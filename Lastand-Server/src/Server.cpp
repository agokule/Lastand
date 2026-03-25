#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <enet/enet.h>
#include <ios>
#include <iostream>
#include <ostream>
#include <string>
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
    std::pair<short, short> player_movement;
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

void parse_client_move(const ENetEvent &event) {
    ClientData &cd {*static_cast<ClientData *>(event.peer->data)};
    ClientMovementTypes movement_type {event.packet->data[1]};
    ClientMovement movement {event.packet->data[2]};
    switch (movement_type) {
        case ClientMovementTypes::Start:
            update_player_delta(movement, false, cd.player_movement);
            break;
        case ClientMovementTypes::Stop:
            update_player_delta(movement, true, cd.player_movement);
            break;
        default:
            std::cerr << "Client movement type not recognized: " << (int)movement_type << std::endl;
    }
    std::cout << "Client movement updated to: " << cd.player_movement.first << ", " << cd.player_movement.second << '\n';
}

void parse_client_shoot(const ENetEvent &event, std::vector<ProjectileDouble> &projectiles) {
    assert(event.packet->dataLength == 13);

    ClientProjectile p {deserialize_client_projectile(IteratorRange<uint8_t*>{&event.packet->data[1], &event.packet->data[12]})};
    ClientData& cd = *static_cast<ClientData *>(event.peer->data);
    ProjectileDouble pd {p, cd.p.id, cd.p.powerups & PowerUp::LongRangeProjectiles};

#ifdef DEBUG
    std::cout << "Shooting projectile: " << pd.x << ", " << pd.y << ", " << p.dx << ", " << p.dy << '\n';
#endif

    projectiles.push_back(pd);
}

void set_client_attributes(const ENetEvent &event, std::map<int, ClientData> &players) {
    SetPlayerAttributesTypes attribute_type {event.packet->data[1]};
    ClientData &cd {*static_cast<ClientData *>(event.peer->data)};
    auto id = cd.p.id;
    switch (attribute_type) {
        case SetPlayerAttributesTypes::UsernameChanged: {
            std::string username;
            int username_len = event.packet->data[2];
            for (int i {3}; i < username_len + 3; i++)
                username.push_back(event.packet->data[i]);
            players.at(id).p.username = username;
            std::cout << "Set username of " << (int)cd.p.id << " to: " << username << '\n';
            std::vector<uint8_t> data_to_send {
                static_cast<uint8_t>(MessageToClientTypes::SetPlayerAttributes),
                static_cast<uint8_t>(SetPlayerAttributesTypes::UsernameChanged),
                static_cast<uint8_t>(id),
                static_cast<uint8_t>(username_len),
            };
            data_to_send.insert(data_to_send.end(), username.begin(), username.end());
            broadcast_packet(event.peer->host, data_to_send, channel_user_updates);
            break;
        }
        case SetPlayerAttributesTypes::ColorChanged: {
            Color c {event.packet->data[2], event.packet->data[3], event.packet->data[4], event.packet->data[5]};
            players.at(id).p.color = c;
            std::cout << "Set color of " << (int)cd.p.id << " to: (" << (int)c.r << ", " << (int)c.g << ", " << (int)c.b << ", " << (int)c.a << ")\n";
            std::vector<uint8_t> data_to_send {
                static_cast<uint8_t>(MessageToClientTypes::SetPlayerAttributes),
                static_cast<uint8_t>(SetPlayerAttributesTypes::ColorChanged),
                static_cast<uint8_t>(id),
                c.r, c.g, c.b, c.a
            };
            broadcast_packet(event.peer->host, data_to_send, channel_user_updates);
            break;
        }
        default:
            std::cerr << "Attribute type not recognized: " << (int)attribute_type << std::endl;
    }
}

void parse_event(const ENetEvent &event, std::vector<ProjectileDouble> &projectiles, std::map<int, ClientData> &players, bool game_started) {
    MessageToServerTypes event_type {event.packet->data[0]};
    if (event.channelID == channel_updates) {
        if (!(
            event_type == MessageToServerTypes::ClientMove ||
            event_type == MessageToServerTypes::Shoot
        )) {
            std::cerr << "Event type not recognized: " << (int)event_type << " " << __FILE__ << ": " << __LINE__ << std::endl;
            return;
        }
        std::cout << "Received event type: " << (int)event_type << std::endl;

        if (event_type == MessageToServerTypes::ClientMove){
            parse_client_move(event);
        } else if (event_type == MessageToServerTypes::Shoot && game_started)
            parse_client_shoot(event, projectiles);
    } else if (event.channelID == channel_user_updates) {
        if (!(event_type == MessageToServerTypes::SetClientAttributes ||
              event_type == MessageToServerTypes::ReadyUp ||
              event_type == MessageToServerTypes::UnReady
        )) {
            std::cerr << "Event type not recognized: " << (int)event_type << " " << __FILE__ << ": " << __LINE__ << std::endl;
            return;
        }
        if (event_type == MessageToServerTypes::SetClientAttributes) {
            set_client_attributes(event, players);
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
        // spawn a new powerup
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
        auto actual_movement = std::make_pair(data.player_movement.first, data.player_movement.second);
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
    ENetEvent event;
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

    auto last_time = std::chrono::high_resolution_clock::now();

    while (running) {
        int err = enet_host_service(server, &event, tick_rate_ms);
        if (err < 0) {
            std::cerr << "An error occurred in enet" << std::endl;
        }

        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                std::cout << "A new client connected from: " << event.peer->address.host << ':' << event.peer->address.port << std::endl;
                if (game_started) {
                    enet_peer_disconnect(event.peer, 0);
                    std::cout << "Game has already started, disconnecting new player" << std::endl;
                }
                players_connected++;
                Player p {default_player};
                p.username += std::to_string(new_player_id);
                p.id = new_player_id;
                p.color = random_color();
                ClientData c {p, false, {0, 0}};
                players[new_player_id] = c;
                event.peer->data = &players.at(new_player_id);

                std::vector<uint8_t> broadcast_data = serialize_player(p);
                broadcast_data.insert(broadcast_data.cbegin(), static_cast<uint8_t>(MessageToClientTypes::PlayerJoined));
                broadcast_packet(server, broadcast_data, channel_events);

                std::cout << "Sending previous game data to player " << new_player_id << std::endl;

                std::vector<Player> other_players;
                for (const auto &[id, data] : players) {
                    if(id == new_player_id)
                        continue;
                    other_players.push_back(data.p);
                }
                std::vector<uint8_t> previous_game_data {serialize_previous_game_data(other_players, obstacles)};

#ifdef DEBUG
                // testing if serializing and deserializing previous game data works
                auto [p2, o2] = deserialize_and_update_previous_game_data(IteratorRange{previous_game_data.cbegin(), previous_game_data.cend()});
                if (p2.size() != other_players.size()) {
                    std::cerr << "slkdjflskdf" << std::endl;
                }
                if (o2.size() != obstacles.size()) {
                    std::cerr << "slkdjflskdf obstacles" << std::endl;
                }
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

                send_packet(event.peer, previous_game_data, channel_events);

                new_player_id++;
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
#ifdef DEBUG
                std::vector<short> data;
                for (int i {0}; i < event.packet->dataLength; i++)
                    data.push_back(event.packet->data[i]);
                std::cout << "A packet of length " << event.packet->dataLength
                        << " containing \"" << data << "\" "
                        << "was received from " << event.peer->address << " "
                        << "from channel " << static_cast<int>(event.channelID) << std::endl;
#endif
                parse_event(event, projectiles, players, game_started);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                std::cout << event.peer->address.host << ':' << event.peer->address.port << " disconnected." << std::endl;
                players_connected--;
                ClientData *c = static_cast<ClientData *>(event.peer->data);
                std::vector<uint8_t> broadcast_data {static_cast<uint8_t>(MessageToClientTypes::PlayerLeft), c->p.id};
                auto player = players.find(c->p.id);
                if (player != players.end()) { // if the player is still alive in the game
                    players.erase(player);
                    broadcast_packet(server, broadcast_data, channel_events);
                }
                break;
            }
            case ENET_EVENT_TYPE_NONE:
                break;
        }
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        if (!game_started) {
            game_started = std::all_of(players.begin(), players.end(), [](const std::pair<int, ClientData> &data) { return data.second.ready; })
                           && players_connected > 1;
            if (game_started) {
                std::cout << "The game has started!" << std::endl;
                broadcast_packet(server, {static_cast<uint8_t>(MessageToClientTypes::GameStarted)}, channel_events);
            }
        }
        if (elapsed_time_ms >= tick_rate_ms || is_within(elapsed_time_ms, tick_rate_ms, 1)) {
            last_time = now;
            auto [dead_players, new_powerup_spawned, powerups_gone] = run_game_tick(players, obstacles, projectiles, powerups_available, game_started);
            for (auto [killed, killer] : dead_players) {
                players.erase(killed);
                std::vector<uint8_t> data_to_send {
                    static_cast<uint8_t>(MessageToClientTypes::PlayerKilled),
                    killer,
                    killed
                };
                broadcast_packet(server, data_to_send, channel_events);
            }
            if (new_powerup_spawned) {
                std::vector<uint8_t> data_to_send {(uint8_t)MessageToClientTypes::NewPowerUpSpawned};
                auto powerup_data = serialize_new_powerup(powerups_available.back());
                data_to_send.insert(data_to_send.end(), powerup_data.begin(), powerup_data.end());
                broadcast_packet(server, data_to_send, channel_updates);
            }

            if (!powerups_gone.empty()) {
                std::vector<uint8_t> data_to_send {(uint8_t)MessageToClientTypes::PowerUpsClaimed};
                data_to_send.push_back(powerups_gone.size());
                for (const auto& [player_id, powerup] : powerups_gone) {
                    data_to_send.push_back(player_id);
                    auto powerup_data = serialize_new_powerup(powerup);
                    data_to_send.insert(data_to_send.end(), powerup_data.begin(), powerup_data.end());
                }

                broadcast_packet(server, data_to_send, channel_updates);
            }

            std::vector<Player> players_to_update;
            players_to_update.reserve(players.size());
            for (const auto &[id, player_data]: players) {
                if (player_data.player_movement == std::make_pair<short, short>(0, 0))
                    continue;
                players_to_update.push_back(player_data.p);
            }

            if (!players_to_update.empty()) {
                std::vector<uint8_t> data_to_send {serialize_game_player_positions(players_to_update)};
                data_to_send.insert(data_to_send.cbegin(), static_cast<uint8_t>(MessageToClientTypes::UpdatePlayerPositions));
                broadcast_packet(server, data_to_send, channel_user_updates, ENET_PACKET_FLAG_UNSEQUENCED);
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
                broadcast_packet(server, projectile_data, channel_updates, ENET_PACKET_FLAG_UNSEQUENCED);
                if (projectiles.empty())
                    sent_empty_projectiles = true;
                else
                    sent_empty_projectiles = false;
            }
            if (game_started && players.size() == 1 && !player_won) {
                std::cout << "The game has ended!" << std::endl;
                broadcast_packet(server, {static_cast<uint8_t>(MessageToClientTypes::PlayerWon), players.begin()->second.p.id}, channel_events);
                player_won = true;
            }
        }
    }

    enet_host_destroy(server);
}
