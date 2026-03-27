#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <enet/enet.h>
#include <ios>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <thread>
#include "Obstacle.h"
#include "random.h"
#include "PowerUps.h"
#include "Projectile.h"
#include "constants.h"
#include "Player.h"
#include "serialize.h"
#include <map>
#include <utility>
#include <vector>
#include <chrono>
#include "physics.h"
#include "utils.h"
#include "ThreadSafeQueue.h"

int players_connected {0};
const int max_players = 100;
const Player default_player {0, 0, {255, 255, 255, 255}, "Player", 0};

// the most top left the player can go
constexpr uint16_t min_x {0};
constexpr uint16_t min_y {0};

// the most bottom right the player can go
constexpr uint16_t max_x {(window_size - player_size) * 2};
constexpr uint16_t max_y {(window_size - player_size) * 2};

// the maximum distance a projectile can travel in pixels
constexpr uint16_t max_obstacle_distance_travelled {500};

struct ClientData {
    Player p;
    bool ready = false;
    std::pair<short, short> player_movement = {0, 0};
    std::pair<short, short> adjusted_player_movement = {0, 0};
    bool player_movement_changed = false;
};

// used in the server to store projectiles with decimal coordinates
struct ProjectileDouble {
    double x;
    double y;
    double dx;
    double dy;
    uint8_t player_id;
    uint16_t start_x;
    uint16_t start_y;
    bool long_range;

    ProjectileDouble(ClientProjectile p, uint8_t player_id, bool long_range)
        : x{static_cast<double>(p.x)}, y{static_cast<double>(p.y)},
          dx{p.dx / std::sqrt(std::pow(p.dx, 2) + std::pow(p.dy, 2))},
          dy{std::sqrt(1 - dx * dx) * (p.dy < 0 ? -1 : 1)},
          player_id{player_id},
          start_x{p.x}, start_y{p.y},
          long_range {long_range}
    {}

    void move(uint8_t times = 1) {
        for (uint8_t i = 0; i < times; i++) {
            x += dx;
            y += dy;
        }
    }
};

// Inbound: network thread pushes these to the game thread
struct InboundEvent {
    enum class Type { Connect, Disconnect, Receive };
    Type type;
    ENetPeer* peer;
    std::vector<uint8_t> data; // packet bytes for Receive events, empty otherwise
    uint8_t channel = 0;
};

// Outbound: game thread pushes these to the network thread
// peer == nullptr means broadcast to all clients
struct OutboundPacket {
    std::vector<uint8_t> data;
    ENetPeer* peer = nullptr;
    int channel;
    ENetPacketFlag flags = ENET_PACKET_FLAG_RELIABLE;
    bool shutdown = false;
};

std::ostream &operator<<(std::ostream &os, const ENetAddress &e) {
    os << e.host << ':' << e.port;
    return os;
}

void send_packet(ENetPeer *peer, const std::vector<uint8_t> &data, int channel_id, ENetPacketFlag flags = ENET_PACKET_FLAG_RELIABLE) {
    std::cout << "Sending packet: " << data << '\n';
    ENetPacket *packet = enet_packet_create(data.data(), data.size(), flags);
    int val = enet_peer_send(peer, channel_id, packet);
    if (val != 0) {
        std::cerr << "Failed to send packet: " << val << " to: " << peer->address << std::endl;
        enet_packet_destroy(packet);
    }
}

void broadcast_packet(ENetHost *server, const std::vector<uint8_t> &data, int channel_id, ENetPacketFlag flags = ENET_PACKET_FLAG_RELIABLE) {
    std::cout << "Broadcasting packet: " << data << '\n';
    ENetPacket *packet = enet_packet_create(data.data(), data.size(), flags);
    enet_host_broadcast(server, channel_id, packet);
}

void parse_client_move(ENetPeer* peer, const std::vector<uint8_t>& data) {
    ClientData &cd {*static_cast<ClientData *>(peer->data)};
    ClientMovementTypes movement_type {data[1]};
    ClientMovement movement {data[2]};
    switch (movement_type) {
        case ClientMovementTypes::Start:
            update_player_delta(movement, false, cd.player_movement);
            cd.player_movement_changed = true;
            break;
        case ClientMovementTypes::Stop:
            update_player_delta(movement, true, cd.player_movement);
            cd.player_movement_changed = true;
            break;
        default:
            std::cerr << "Client movement type not recognized: " << (int)movement_type << std::endl;
    }
    std::cout << "Client movement updated to: " << cd.player_movement.first << ", " << cd.player_movement.second << '\n';
}

void parse_client_shoot(ENetPeer* peer, const std::vector<uint8_t>& data, std::vector<ProjectileDouble> &projectiles) {
    assert(data.size() == 13);
    ClientProjectile p {deserialize_client_projectile(IteratorRange<const uint8_t*>{data.data() + 1, data.data() + 12})};
    ClientData& cd = *static_cast<ClientData *>(peer->data);
    ProjectileDouble pd {p, cd.p.id, cd.p.powerups & PowerUp::LongRangeProjectiles};

#ifdef DEBUG
    std::cout << "Shooting projectile: " << pd.x << ", " << pd.y << ", " << p.dx << ", " << p.dy << '\n';
#endif

    projectiles.push_back(pd);
}

void set_client_attributes(ENetPeer* peer, const std::vector<uint8_t>& data, std::map<int, ClientData> &players, ThreadSafeQueue<OutboundPacket>& outbound) {
    SetPlayerAttributesTypes attribute_type {data[1]};
    ClientData &cd {*static_cast<ClientData *>(peer->data)};
    auto id = cd.p.id;
    switch (attribute_type) {
        case SetPlayerAttributesTypes::UsernameChanged: {
            std::string username;
            int username_len = data[2];
            for (int i {3}; i < username_len + 3; i++)
                username.push_back(data[i]);
            players.at(id).p.username = username;
            std::cout << "Set username of " << (int)cd.p.id << " to: " << username << '\n';
            std::vector<uint8_t> data_to_send {
                static_cast<uint8_t>(MessageToClientTypes::SetPlayerAttributes),
                static_cast<uint8_t>(SetPlayerAttributesTypes::UsernameChanged),
                static_cast<uint8_t>(id),
                static_cast<uint8_t>(username_len),
            };
            data_to_send.insert(data_to_send.end(), username.begin(), username.end());
            outbound.push({std::move(data_to_send), nullptr, channel_user_updates});
            break;
        }
        case SetPlayerAttributesTypes::ColorChanged: {
            Color c {data[2], data[3], data[4], data[5]};
            players.at(id).p.color = c;
            std::cout << "Set color of " << (int)cd.p.id << " to: (" << (int)c.r << ", " << (int)c.g << ", " << (int)c.b << ", " << (int)c.a << ")\n";
            std::vector<uint8_t> data_to_send {
                static_cast<uint8_t>(MessageToClientTypes::SetPlayerAttributes),
                static_cast<uint8_t>(SetPlayerAttributesTypes::ColorChanged),
                static_cast<uint8_t>(id),
                c.r, c.g, c.b, c.a
            };
            outbound.push({std::move(data_to_send), nullptr, channel_user_updates});
            break;
        }
        default:
            std::cerr << "Attribute type not recognized: " << (int)attribute_type << std::endl;
    }
}

void parse_event(const InboundEvent& event, std::vector<ProjectileDouble> &projectiles, std::map<int, ClientData> &players, bool game_started, ThreadSafeQueue<OutboundPacket>& outbound) {
    MessageToServerTypes event_type {event.data[0]};
    if (event.channel == channel_updates) {
        if (!(
            event_type == MessageToServerTypes::ClientMove ||
            event_type == MessageToServerTypes::Shoot
        )) {
            std::cerr << "Event type not recognized: " << (int)event_type << " " << __FILE__ << ": " << __LINE__ << std::endl;
            return;
        }
        std::cout << "Received event type: " << (int)event_type << std::endl;

        if (event_type == MessageToServerTypes::ClientMove)
            parse_client_move(event.peer, event.data);
        else if (event_type == MessageToServerTypes::Shoot && game_started)
            parse_client_shoot(event.peer, event.data, projectiles);
    } else if (event.channel == channel_user_updates) {
        if (!(event_type == MessageToServerTypes::SetClientAttributes ||
              event_type == MessageToServerTypes::ReadyUp ||
              event_type == MessageToServerTypes::UnReady
        )) {
            std::cerr << "Event type not recognized: " << (int)event_type << " " << __FILE__ << ": " << __LINE__ << std::endl;
            return;
        }
        if (event_type == MessageToServerTypes::SetClientAttributes) {
            set_client_attributes(event.peer, event.data, players, outbound);
        } else if (event_type == MessageToServerTypes::ReadyUp) {
            std::cout << "Player " << players.at(static_cast<ClientData *>(event.peer->data)->p.id).p.id << " is ready\n";
            players.at(static_cast<ClientData *>(event.peer->data)->p.id).ready = true;
        } else if (event_type == MessageToServerTypes::UnReady) {
            std::cout << "Player " << players.at(static_cast<ClientData *>(event.peer->data)->p.id).p.id << " is not ready\n";
            players.at(static_cast<ClientData *>(event.peer->data)->p.id).ready = false;
        }
    }
}

struct GameTickResult {
    std::map<uint8_t, uint8_t> dead_players;
    bool new_powerup_spawned = false;
    std::map<uint8_t, NewPowerUp> powerups_lost;
};

GameTickResult run_game_tick(
    std::map<int, ClientData> &players,
    const std::vector<Obstacle> &obstacles,
    std::vector<ProjectileDouble> &projectiles,
    std::vector<NewPowerUp> &powerups_available,
    bool game_started
) {
    GameTickResult result;

    // check if powerup was claimed
    for (auto it = powerups_available.begin(); it != powerups_available.end(); it++) {
        bool collision = false;
        auto& p = *it;
        for (auto& data : players) {
            auto& player = data.second.p;
            if (!point_in_rect(player.x, player.y, player_size, player_size, p.x, p.y))
                continue;

            player.powerups |= p.powerup;
            result.powerups_lost.emplace(player.id, p);
            collision = true;
            break;
        }
        if (collision) {
            it = powerups_available.erase(it);
            if (it == powerups_available.end())
                break;
        }
    }

    // spawn new powerup
    if (random(10'000) == 1 && game_started) {
        result.new_powerup_spawned = true;
        uint16_t x = 0, y = 0;
        while (true) {
            x = random(max_x);
            y = random(max_y);
            if (std::all_of(obstacles.begin(), obstacles.end(),
                            [x, y](const Obstacle& o) {
                                return !point_in_rect(o.x, o.y, o.width, o.height, x, y);
                            }) &&
                std::all_of(powerups_available.begin(), powerups_available.end(),
                            [x, y](const NewPowerUp& p) {
                                return (p.x != x || p.y != y);
                            })
            )
                break;
        }
        auto power = random(3);
        PowerUp powerup;
        switch (power) {
            case 0:
                powerup = PowerUp::PhaseThroughObstacles;
                break;
            case 1:
                powerup = PowerUp::Speed;
                break;
            case 2:
                powerup = PowerUp::LongRangeProjectiles;
        }
        powerups_available.emplace_back(x, y, powerup);
    }

    for (auto &[id, data] : players) {
        if (data.player_movement == std::make_pair<short, short>(0, 0))
            continue;
        auto orig_adj = data.adjusted_player_movement;
        auto actual_movement = std::make_pair(data.player_movement.first, data.player_movement.second);
        // FIXME: currently, if you have the speed powerup and go to the top left corner
        // of the map, an integer underflow will happen and you will be banished forever
        if ((data.p.x <= min_x && actual_movement.first == -1) ||
            (data.p.x >= max_x && actual_movement.first == 1)) {
            actual_movement.first = 0;
        }
        if ((data.p.y <= min_y && actual_movement.second == -1) ||
            (data.p.y >= max_y && actual_movement.second == 1)) {
            actual_movement.second = 0;
        }
        if (actual_movement == std::make_pair<short, short>(0, 0))
            continue;
        Player test_px {data.p};
        test_px.move(std::make_pair(data.player_movement.first, 0));
        if (data.p.powerups & PowerUp::Speed)
            test_px.move(std::make_pair(data.player_movement.first, 0));
        auto collision_x = detect_collision(test_px, obstacles);

        Player test_py {data.p};
        test_py.move(std::make_pair(0, data.player_movement.second));
        if (data.p.powerups & PowerUp::Speed)
            test_py.move(std::make_pair(0, data.player_movement.second));
        auto collision_y = detect_collision(test_py, obstacles);

#ifdef DEBUG
        std::cout << "Collision x: " << collision_x << ", Collision y: " << collision_y << '\n';
#endif

        if (collision_x && !(data.p.powerups & PowerUp::PhaseThroughObstacles))
            actual_movement.first = 0;
        if (collision_y && !(data.p.powerups & PowerUp::PhaseThroughObstacles))
            actual_movement.second = 0;
        data.adjusted_player_movement = actual_movement;
        if (data.adjusted_player_movement != orig_adj)
            data.player_movement_changed = true;
        data.p.move(actual_movement);
        if (data.p.powerups & PowerUp::Speed)
            data.p.move(actual_movement);
#ifdef DEBUG
        if (actual_movement != std::make_pair<short, short>(0, 0))
            std::cout << "Player moved to " << id << ": " << data.p.x << ", " << data.p.y << '\n';
#endif
    }
    for (auto it = projectiles.begin(); it != projectiles.end(); it++) {
        auto& p = *it;
        p.move(4);
        Player player_that_got_hit;
        bool hit_player = std::any_of(
            players.begin(), players.end(),
            [p, &player_that_got_hit](const std::pair<uint8_t, ClientData> &data) {
                if (point_in_rect(data.second.p.x, data.second.p.y, player_size, player_size, p.x, p.y) &&
                    data.second.p.id != p.player_id)
                {
                    player_that_got_hit = data.second.p;
                    return true;
                }
                return false;
        });
        double distance_travelled = std::sqrt(std::pow(p.x - p.start_x, 2) + std::pow(p.y - p.start_y, 2));
        auto max_distance = max_obstacle_distance_travelled + (p.long_range ? max_obstacle_distance_travelled : 0);
        if (p.x > max_x || p.y > max_y + player_size || p.x < min_x || p.y < min_y || (hit_player) || distance_travelled >= max_distance ||
            std::any_of(obstacles.begin(), obstacles.end(), 
                        [p](Obstacle ob) { return point_in_rect(ob.x, ob.y, ob.width, ob.height, p.x, p.y); })
        ) {
            if (hit_player) {
                // someone got hit and died
                result.dead_players[player_that_got_hit.id] = p.player_id;
            }
            it = projectiles.erase(it);
            if (it == projectiles.end())
                break;
        }
    }
    return result;
}

void networking(
    ENetHost* server,
    ThreadSafeQueue<InboundEvent>& inbound,
    ThreadSafeQueue<OutboundPacket>& outbound
) {
    using namespace std::chrono_literals;
    bool running = true;

    while (running) {
        // drain the outbound queue and send all pending packets first
        std::optional<OutboundPacket> pkt;
        while ((pkt = outbound.try_pop()).has_value()) {
            if (pkt->shutdown) {
                running = false;
                break;
            }
            if (pkt->peer == nullptr)
                broadcast_packet(server, pkt->data, pkt->channel, pkt->flags);
            else
                send_packet(pkt->peer, pkt->data, pkt->channel, pkt->flags);
        }
        if (!running)
            break;

        // receive all pending network events and push them to the game thread
        ENetEvent event;
        while (enet_host_service(server, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT:
                    inbound.push({InboundEvent::Type::Connect, event.peer, {}, 0});
                    break;
                case ENET_EVENT_TYPE_RECEIVE: {
                    std::vector<uint8_t> data(event.packet->data, event.packet->data + event.packet->dataLength);
#ifdef DEBUG
                    std::cout << "A packet of length " << event.packet->dataLength
                              << " containing \"" << data << "\" "
                              << "was received on channel " << static_cast<int>(event.channelID) << std::endl;
#endif
                    inbound.push({InboundEvent::Type::Receive, event.peer, std::move(data), event.channelID});
                    enet_packet_destroy(event.packet);
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT:
                    inbound.push({InboundEvent::Type::Disconnect, event.peer, {}, 0});
                    break;
                default:
                    break;
            }
        }

        std::this_thread::sleep_for(1ms);
    }
}

int main(int argv, char **argc) {
    if (enet_initialize() != 0) {
        std::cerr << "Couldn't initialize enet" << std::endl;
        return 1;
    }
    std::atexit(enet_deinitialize);
    std::cout << std::boolalpha;

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = 8888;
    if (argv > 1)
        address.port = std::stoi(argc[1]);

    ENetHost *server {enet_host_create(&address, max_players, num_channels, 0, 0)};
    if (server == NULL) {
        std::cerr << "Couldn't initialize ENetHost" << std::endl;
        return 1;
    }

    std::map<int, ClientData> players;
    std::vector<ProjectileDouble> projectiles;
    std::vector<NewPowerUp> powerups_available;
    int new_player_id {0};

    bool running = true;
    std::cout << "hosting on port " << address.port << std::endl;
    bool game_started = false;

    // map3 kind of looks cool
    // map5 has a big wall
    const std::vector<Obstacle> obstacles {load_from_file("maps/map2.txt")};
    std::cout << "Loaded " << obstacles.size() << " obstacles" << std::endl;
    // whether the server should send a list of empty projectiles
    bool sent_empty_projectiles = false;
    bool player_won = false;

#if defined(DEBUG)
    for (const auto &o : obstacles) {
        std::cout << "Read obstacle at: (" << o.x << ", " << o.y << ") (" << o.width << ", " << o.height << ")"
            << "(" << (int)o.color.r << ", " << (int)o.color.g << ", " << (int)o.color.b << ", " << (int)o.color.a << ")" << std::endl;
        auto data = serialize_obstacle(o);
        std::cout << "Correct obstacle serialized: " << data << std::endl;
    }
#endif

    ThreadSafeQueue<InboundEvent> inbound_queue;
    ThreadSafeQueue<OutboundPacket> outbound_queue;

    std::thread network_thread([&]() {
        networking(server, inbound_queue, outbound_queue);
    });

    auto last_time = std::chrono::steady_clock::now();

    while (running) {
        // drain all inbound events from the network thread
        std::optional<InboundEvent> evt;
        while ((evt = inbound_queue.try_pop()).has_value()) {
            switch (evt->type) {
                case InboundEvent::Type::Connect: {
                    std::cout << "A new client connected from: " << evt->peer->address.host << ':' << evt->peer->address.port << std::endl;
                    if (game_started) {
                        // tell the network thread to disconnect this peer immediately
                        outbound_queue.push({{}, evt->peer, 0, ENET_PACKET_FLAG_RELIABLE, false});
                        std::cout << "Game has already started, disconnecting new player" << std::endl;
                        break;
                    }
                    players_connected++;
                    Player p {default_player};
                    p.username += std::to_string(new_player_id);
                    p.id = new_player_id;
                    p.color = random_color();
                    ClientData c {p, false, {0, 0}};
                    players[new_player_id] = c;
                    // peer->data is set here on the game thread; the network thread never touches it
                    evt->peer->data = &players.at(new_player_id);

                    std::vector<uint8_t> broadcast_data = serialize_player(p);
                    broadcast_data.insert(broadcast_data.cbegin(), static_cast<uint8_t>(MessageToClientTypes::PlayerJoined));
                    outbound_queue.push({broadcast_data, nullptr, channel_events});

                    std::cout << "Sending previous game data to player " << new_player_id << std::endl;
                    std::vector<Player> other_players;
                    for (const auto &[id, data] : players) {
                        if (id == new_player_id)
                            continue;
                        other_players.push_back(data.p);
                    }
                    std::vector<uint8_t> previous_game_data {serialize_previous_game_data(other_players, obstacles)};

#ifdef DEBUG
                    // testing if serializing and deserializing previous game data works
                    auto [p2, o2] = deserialize_and_update_previous_game_data(IteratorRange{previous_game_data.cbegin(), previous_game_data.cend()});
                    if (p2.size() != other_players.size())
                        std::cerr << "slkdjflskdf" << std::endl;
                    if (o2.size() != obstacles.size())
                        std::cerr << "slkdjflskdf obstacles" << std::endl;
                    for (size_t i {0}; i < p2.size(); i++) {
                        auto p1 {other_players[i]};
                        auto p3 {p2.at(p1.id)};
                        if (p1.id != p3.id || p1.username != p3.username || p1.x != p3.x || p1.y != p3.y || p1.color.r != p3.color.r || p1.color.g != p3.color.g || p1.color.b != p3.color.b || p1.color.a != p3.color.a) {
                            std::cerr << "slkdjflskdf player is different\n"
                                      << "player1: " << p1.username << "(" << (int)p1.x << ", " << (int)p1.y << ")" << "(" << (int)p1.color.r << ", " << (int)p1.color.g << ", " << (int)p1.color.b << ", " << (int)p1.color.a << ")\n"
                                      << " player2: " << p3.username << "(" << (int)p3.x << ", " << (int)p3.y << ")" << "(" << (int)p3.color.r << ", " << (int)p3.color.g << ", " << (int)p3.color.b << ", " << (int)p3.color.a << ")" << std::endl;
                        }
                    }
                    std::cout << "Checking obstacles" << std::endl;
                    for (size_t i {0}; i < o2.size(); i++) {
                        std::cout << "Checking obstacle " << i << std::endl;
                        auto o1 {obstacles[i]};
                        auto o3 {o2[i]};
                        if (o1.x != o3.x || o1.y != o3.y || o1.width != o3.width || o1.height != o3.height || o1.color.r != o3.color.r || o1.color.g != o3.color.g || o1.color.b != o3.color.b || o1.color.a != o3.color.a)
                            std::cerr << "slkdjflskdf obstacle is different " << std::endl;
                    }
#endif

                    previous_game_data.insert(previous_game_data.begin(), static_cast<uint8_t>(MessageToClientTypes::PreviousGameData));
                    outbound_queue.push({previous_game_data, evt->peer, channel_events});

                    new_player_id++;
                    break;
                }
                case InboundEvent::Type::Receive: {
                    parse_event(*evt, projectiles, players, game_started, outbound_queue);
                    break;
                }
                case InboundEvent::Type::Disconnect: {
                    std::cout << evt->peer->address.host << ':' << evt->peer->address.port << " disconnected." << std::endl;
                    players_connected--;
                    ClientData *c = static_cast<ClientData *>(evt->peer->data);
                    std::vector<uint8_t> broadcast_data {static_cast<uint8_t>(MessageToClientTypes::PlayerLeft), c->p.id};
                    auto player = players.find(c->p.id);
                    if (player != players.end()) {
                        players.erase(player);
                        outbound_queue.push({broadcast_data, nullptr, channel_events});
                    }
                    break;
                }
            }
        }

        using std::chrono::duration_cast;
        auto now = std::chrono::steady_clock::now();
        auto now_ms = duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        auto elapsed_time_ms = duration_cast<std::chrono::milliseconds>(now - last_time).count();

        if (!game_started) {
            game_started = std::all_of(players.begin(), players.end(), [](const std::pair<int, ClientData> &data) { return data.second.ready; })
                           && players_connected > 1;
            if (game_started) {
                std::cout << "The game has started!" << std::endl;
                outbound_queue.push({{static_cast<uint8_t>(MessageToClientTypes::GameStarted)}, nullptr, channel_events});
            }
        }

        if (elapsed_time_ms >= tick_rate_ms || is_within(elapsed_time_ms, tick_rate_ms, 1)) {
            last_time = now;
            auto [dead_players, new_powerup_spawned, powerups_gone] = run_game_tick(players, obstacles, projectiles, powerups_available, game_started);

            for (auto [killed, killer] : dead_players) {
                players.erase(killed);
                outbound_queue.push({{
                    static_cast<uint8_t>(MessageToClientTypes::PlayerKilled),
                    killer,
                    killed
                }, nullptr, channel_events});
            }

            if (new_powerup_spawned) {
                std::vector<uint8_t> data_to_send {(uint8_t)MessageToClientTypes::NewPowerUpSpawned};
                auto powerup_data = serialize_new_powerup(powerups_available.back());
                data_to_send.insert(data_to_send.end(), powerup_data.begin(), powerup_data.end());
                outbound_queue.push({std::move(data_to_send), nullptr, channel_updates});
            }

            if (!powerups_gone.empty()) {
                std::vector<uint8_t> data_to_send {(uint8_t)MessageToClientTypes::PowerUpsClaimed};
                data_to_send.push_back(powerups_gone.size());
                for (const auto& [player_id, powerup] : powerups_gone) {
                    data_to_send.push_back(player_id);
                    auto powerup_data = serialize_new_powerup(powerup);
                    data_to_send.insert(data_to_send.end(), powerup_data.begin(), powerup_data.end());
                }
                outbound_queue.push({std::move(data_to_send), nullptr, channel_updates});
            }

            std::vector<Player> players_to_update;
            std::vector<ClientMovementUpdate> player_movement_changes;
            players_to_update.reserve(players.size());
            player_movement_changes.reserve(players.size());
            for (auto &[id, player_data]: players) {
                if (player_data.player_movement == std::make_pair<short, short>(0, 0))
                    player_data.adjusted_player_movement = {0, 0};

                if (player_data.adjusted_player_movement != std::make_pair<short, short>(0, 0) ||
                    player_data.player_movement_changed
                ) {
                    players_to_update.push_back(player_data.p);

                }
                if (player_data.player_movement_changed) {
                    player_movement_changes.push_back({player_data.p.id, create_player_movement(player_data.adjusted_player_movement)});
                    player_data.player_movement_changed = false;
                }
            }

            if (!player_movement_changes.empty()) {
                std::vector<uint8_t> data_to_send {
                    serialize_client_movement_update(IteratorRange {player_movement_changes.cbegin(), player_movement_changes.cend()})
                };
                data_to_send.insert(data_to_send.begin(), static_cast<uint8_t>(MessageToClientTypes::UpdatePlayerMovement));
                outbound_queue.push({std::move(data_to_send), nullptr, channel_updates, ENET_PACKET_FLAG_RELIABLE});
            }

            if (is_within(now_ms % 100, 50, 10) && !players_to_update.empty()) {
                std::vector<uint8_t> data_to_send {serialize_game_player_positions(players_to_update)};
                data_to_send.insert(data_to_send.cbegin(), static_cast<uint8_t>(MessageToClientTypes::UpdatePlayerPositions));
                outbound_queue.push({std::move(data_to_send), nullptr, channel_updates, ENET_PACKET_FLAG_UNSEQUENCED});
            }

            if (!projectiles.empty() || !sent_empty_projectiles) {
                std::vector<uint8_t> projectile_data;
                projectile_data.reserve(2 + projectiles.size() * sizeof(Projectile));
                projectile_data.push_back(static_cast<uint8_t>(MessageToClientTypes::UpdateProjectiles));
                projectile_data.push_back(static_cast<uint8_t>(projectiles.size()));
                for (auto &pd: projectiles) {
                    Projectile p {static_cast<uint16_t>(pd.x), static_cast<uint16_t>(pd.y)};
#ifdef DEBUG
                    printf("Projectile: (%d, %d)\n", p.x, p.y);
#endif
                    auto p_data = serialize_projectile(p);
                    projectile_data.insert(projectile_data.end(), p_data.cbegin(), p_data.cend());
                }
                outbound_queue.push({std::move(projectile_data), nullptr, channel_updates, ENET_PACKET_FLAG_UNSEQUENCED});
                sent_empty_projectiles = projectiles.empty();
            }

            if (game_started && players.size() == 1 && !player_won) {
                std::cout << "The game has ended!" << std::endl;
                outbound_queue.push({{
                    static_cast<uint8_t>(MessageToClientTypes::PlayerWon),
                    players.begin()->second.p.id
                }, nullptr, channel_events});
                player_won = true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    outbound_queue.push({{}, nullptr, 0, ENET_PACKET_FLAG_RELIABLE, true});
    network_thread.join();
    enet_host_destroy(server);
}
