#include <SDL3/SDL.h>
#include "ThreadSafeQueue.h"
#include <chrono>
#include "Obstacle.h"
#include "Player.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <SDL3/SDL_main.h>
#include "PowerUps.h"
#include "Projectile.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_timer.h"
#include "SDL3/SDL_video.h"
#include "constants.h"
#include <enet/enet.h>
#include <iterator>
#include <optional>
#include <sstream>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>
#include "serialize.h"
#include <map>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_stdlib.h"
#include "utils.h"

const uint16_t window_height {window_size + 100};
static bool spectating = false;
constexpr const char* restart_client = "No one left now";

#define LEFT_ARROW "\U0000f30a" // Looks kind of like this: ←
#define RIGHT_ARROW "\U0000f30b" // Looks kind of like this: →

struct Particle {
    float x, y;
    float dx, dy;
    float life_left;
    SDL_Color color;

    void update() {
        x += dx;
        y += dy;
        life_left -= 0.2;
    }
};

template <size_t N>
std::array<Particle, N> create_particles(int start_x, int start_y, float life = -1) {
    std::array<Particle, N> particles;
    for (auto &p : particles) {
        p.x = start_x;
        p.y = start_y;
        p.dx = SDL_rand(6) - 3;
        p.dy = SDL_rand(6) - 3;
        p.life_left = life == -1 ? SDL_rand(15) + 5 : life;
        p.color = {static_cast<Uint8>(SDL_rand(155) + 100), static_cast<Uint8>(SDL_rand(155) + 30), 0, 255};
    }
    return particles;
}

void update_particles(std::vector<Particle> &particles) {
    for (auto p = particles.begin(); p != particles.end(); ++p) {
        p->update();
        if (p->life_left <= 0) {
            p = particles.erase(p);
            if (p == particles.end())
                break;
        }
    }
}

void draw_particles(SDL_Renderer *renderer, const std::vector<Particle> &particles) {
    for (const auto &p : particles) {
        SDL_FRect frect {p.x, p.y, 3.0, 3.0};
        SDL_SetRenderDrawColor(renderer, p.color.r, p.color.g, p.color.b, p.color.a);
        SDL_RenderFillRect(renderer, &frect);
    }
}

void draw_player(SDL_Renderer *renderer, const Player &p, const Player& local_player) {
    SDL_FRect frect {
        static_cast<float>(p.x - local_player.x + window_size / 2.0f),
        static_cast<float>(p.y - local_player.y + window_size / 2.0f),
        player_size, player_size
    };
    if (local_player.id == p.id) {
        frect.x = local_player.x;
        frect.y = local_player.y;
    }
    SDL_FRect shadow_frect {frect.x + 3, frect.y + 3, frect.w, frect.h};
    bool success;

    success = SDL_SetRenderDrawColor(renderer, p.color.r, p.color.g, p.color.b, 100);
    if (!success) std::cerr << "Error in SDL_SetRenderDrawColor: " << SDL_GetError();
    success = SDL_RenderFillRect(renderer, &shadow_frect);
    if (!success) std::cerr << "Error in SDL_RenderFillRect: " << SDL_GetError();

    success = SDL_SetRenderDrawColor(renderer, p.color.r, p.color.g, p.color.b, p.color.a);
    if (!success) std::cerr << "Error in SDL_SetRenderDrawColor: " << SDL_GetError();
    success = SDL_RenderFillRect(renderer, &frect);
    if (!success) std::cerr << "Error in SDL_RenderFillRect: " << SDL_GetError();
}

void draw_player_username(const Player &p, const Player& local_player) {
    ImVec2 next_window_pos {
        (int)p.x - local_player.x - 9 + window_size / 2.0f,
        (int)p.y - local_player.y - 20 + window_size / 2.0f
    };
    if (local_player.id == p.id) {
        next_window_pos.x -= window_size / 2.0f;
        next_window_pos.y -= window_size / 2.0f;

        next_window_pos.x += local_player.x;
        next_window_pos.y += local_player.y;
    }
    ImGui::SetNextWindowPos(next_window_pos);
    ImGui::Begin(p.username.c_str(), nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    std::string username_to_display = p.username;
    if (ImGui::CalcTextSize(username_to_display.c_str()).x > 30)
        username_to_display = username_to_display.substr(0, 5) + "...";
    ImGui::PushFont(nullptr, 11.0f);
    ImGui::Text("%s", username_to_display.c_str());
    ImGui::PopFont();
    ImGui::End();
}

void draw_this_player(SDL_Renderer *renderer, Player &p) {
    auto orig_x = p.x;
    auto orig_y = p.y;

    p.x = window_size / 2;
    p.y = window_size / 2;

    draw_player(renderer, p, p);
    draw_player_username(p, p);

    p.x = orig_x;
    p.y = orig_y;
}

void draw_obstacle(SDL_Renderer *renderer, const Obstacle &o, const Player& local_player) {
    SDL_FRect frect {
        static_cast<float>(o.x - local_player.x + window_size / 2.0f),
        static_cast<float>(o.y - local_player.y + window_size / 2.0f),
        static_cast<float>(o.width),
        static_cast<float>(o.height)
    };
    bool success = SDL_SetRenderDrawColor(renderer, o.color.r, o.color.g, o.color.b, o.color.a);
    if (!success) std::cerr << "Error in SDL_SetRenderDrawColor: " << SDL_GetError();
    success = SDL_RenderFillRect(renderer, &frect);
    if (!success) std::cerr << "Error in SDL_RenderFillRect: " << SDL_GetError();
}

void draw_point(SDL_Renderer *renderer, const Player& local_player, SDL_Color color, std::pair<uint16_t, uint16_t> coordinates) {
    SDL_FRect frect {
        static_cast<float>(coordinates.first - local_player.x + window_size / 2.0f),
        static_cast<float>(coordinates.second - local_player.y + window_size / 2.0f),
        3.0, 3.0
    };
    bool success = SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    if (!success) std::cerr << "Error in SDL_SetRenderDrawColor: " << SDL_GetError();
    success = SDL_RenderFillRect(renderer, &frect);
    if (!success) std::cerr << "Error in SDL_RenderFillRect: " << SDL_GetError();
}

void draw_projectile(SDL_Renderer *renderer, const Projectile &p, const Player& local_player) {
    draw_point(renderer, local_player, {255, 0, 0, 255}, {p.x, p.y});
}

void draw_powerup(SDL_Renderer *renderer, const NewPowerUp &p, const Player& local_player) {
    SDL_Color color {0, 0, 0, 255};
    switch (p.powerup) {
        case PowerUp::LongRangeProjectiles:
            color.r = 120;
            color.g = 120;
            break;
        case PowerUp::PhaseThroughObstacles:
            color.r = 120;
            color.b = 120;
            break;
        case PowerUp::Speed:
            color.g = 120;
            color.b = 120;
            break;
    }

    draw_point(renderer, local_player, color, {p.x, p.y});
}

const std::string window_title {"Lastand Client"};

ClientMovement create_client_movement(SDL_Scancode key) {
    ClientMovement m;
    switch (key) {
        case SDL_SCANCODE_UP:
        case SDL_SCANCODE_W:
            m = ClientMovement::Up;
            break;
        case SDL_SCANCODE_DOWN:
        case SDL_SCANCODE_S:
            m = ClientMovement::Down;
            break;
        case SDL_SCANCODE_LEFT:
        case SDL_SCANCODE_A:
            m = ClientMovement::Left;
            break;
        case SDL_SCANCODE_RIGHT:
        case SDL_SCANCODE_D:
            m = ClientMovement::Right;
            break;
        default:
            m = ClientMovement::None;
    }
    return m;
}

std::vector<uint8_t> handle_key_down(SDL_Scancode key) {
    auto movement = create_client_movement(key);
    std::vector<uint8_t> msg {
        static_cast<uint8_t>(MessageToServerTypes::ClientMove),
        static_cast<uint8_t>(ClientMovementTypes::Start),
        static_cast<uint8_t>(movement)
    };
    return msg;
}

std::vector<uint8_t> handle_key_down(SDL_Scancode key, std::pair<short, short> &player_delta) {
    auto movement = create_client_movement(key);
    std::vector<uint8_t> msg {
        static_cast<uint8_t>(MessageToServerTypes::ClientMove),
        static_cast<uint8_t>(ClientMovementTypes::Start),
        static_cast<uint8_t>(movement)
    };
    update_player_delta(movement, false, player_delta);
    return msg;
}

std::vector<uint8_t> handle_key_up(SDL_Scancode key, std::pair<short, short> &player_delta) {
    auto movement = create_client_movement(key);
    std::vector<uint8_t> msg {
        static_cast<uint8_t>(MessageToServerTypes::ClientMove),
        static_cast<uint8_t>(ClientMovementTypes::Stop),
        static_cast<uint8_t>(movement)
    };
    update_player_delta(movement, true, player_delta);
    return msg;
}

std::vector<uint8_t> handle_mouse_up(const Player& local_player, SDL_MouseButtonEvent event) {
    auto y = static_cast<uint16_t>(local_player.y + player_size / 2.0f);
    auto x = static_cast<uint16_t>(local_player.x + player_size / 2.0f);
    ClientProjectile p {x, y, static_cast<int32_t>(event.x - window_size / 2.0f), static_cast<int32_t>(event.y - window_size / 2.0f)};
    std::cout << "Projectile: (" << p.x << ", " << p.y << ")(" << p.dx << ", " << p.dy << ")\n";
    auto data = serialize_client_projectile(p);
    std::vector<uint8_t> msg {
        static_cast<uint8_t>(MessageToServerTypes::Shoot)
    };
    msg.insert(msg.end(), data.begin(), data.end());
    return msg;
}

std::vector<uint8_t> process_event(const SDL_Event &event, std::pair<short, short> &player_delta, const Player& local_player) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            return handle_key_down(event.key.scancode, player_delta);
        case SDL_EVENT_KEY_UP:
            return handle_key_up(event.key.scancode, player_delta);
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return handle_mouse_up(local_player, event.button);
        default:
            return {};
    }
}

std::string parse_message_from_server(
    const std::vector<uint8_t> &data,
    std::map<int, Player> &player_data,
    std::vector<Projectile> &projectiles,
    std::vector<Particle> &particles,
    std::vector<NewPowerUp>& powerups,
    uint8_t& local_player_id,
    SDL_Window* window
) {
    MessageToClientTypes type {data[0]};
    IteratorRange data_without_type {data.cbegin() + 1, data.cend()};
    switch (type) {
        case MessageToClientTypes::UpdatePlayerPositions: {
#ifdef DEBUG
            std::cout << "update player positions" << '\n';
#endif
            deserialize_and_update_game_player_positions(data_without_type, player_data);
            break;
        }
        case MessageToClientTypes::PlayerJoined: {
            std::cout << "Player joined" << std::endl;
            Player p {deserialize_player(data_without_type)};
            player_data[p.id] = p;
            return std::string("Player ") + p.username + " joined";
            break;
        }
        case MessageToClientTypes::PlayerLeft: {
            int id {data_without_type[0]};
            std::cout << "Player " << id << " left" << std::endl;
            std::string username = player_data.at(id).username;

            player_data.erase(id);
            if (id == local_player_id) {
                if (!player_data.empty())
                    local_player_id = player_data.begin()->first;
                else {
                    SDL_ShowSimpleMessageBox(
                        SDL_MESSAGEBOX_INFORMATION,
                        "Game Over",
                        "No one else is connected, closing game now",
                        window
                    );
                    return restart_client;
                }
            }

            return username + " left";
            break;
        }
        case MessageToClientTypes::UpdateProjectiles: {
            projectiles.clear();
            for (size_t i = 1, proj = 0; i < data_without_type.size() && proj < data_without_type[0]; i += sizeof(Projectile), proj++) {
                std::array<uint8_t, 4> data {
                    data_without_type[i],
                    data_without_type[i + 1],
                    data_without_type[i + 2],
                    data_without_type[i + 3],
                };
                projectiles.push_back(deserialize_projectile(data));
            }
            break;
        }
        case MessageToClientTypes::PlayerKilled: {
            assert(data_without_type.size() == 2);
            uint8_t killer {data_without_type[0]};
            uint8_t killed {data_without_type[1]};
            std::stringstream ss;
            ss << player_data.at(killer).username << " has killed " << player_data.at(killed).username;

            // add particles
            int start_x = player_data.at(killed).x / 2 + player_size - (player_data.at(local_player_id).x + window_size / 2);
            int start_y = player_data.at(killed).y / 2 + player_size - (player_data.at(local_player_id).y + window_size / 2);
            auto new_particles = create_particles<15>(start_x, start_y);
            particles.insert(particles.end(), new_particles.begin(), new_particles.end());

            if (killed == local_player_id) {
                spectating = true;
                local_player_id = killer;
                ss << "\nNow spectating " << player_data.at(local_player_id).username << '\n';
            }

            std::cout << ss.str() << std::endl;
            player_data.erase(killed);
            return ss.str();
            break;
        }
        case MessageToClientTypes::GameStarted: {
            std::cout << "The game has started!" << std::endl;
            return "The game has started!";
            break;
        }
        case MessageToClientTypes::SetPlayerAttributes: {
            SetPlayerAttributesTypes attribute_type = static_cast<SetPlayerAttributesTypes>(data_without_type[0]);
            auto player_id = data_without_type[1];
            std::stringstream ss;
            std::cout << "Player set attribute: " << (int)player_id << " " << (int)attribute_type << std::endl;
            switch (attribute_type) {
                case SetPlayerAttributesTypes::UsernameChanged: {
                    std::string username {data_without_type.start + 3, data_without_type.end};
                    std::cout << "Set username of " << (int)player_id << " to: " << username << '\n';
                    ss << player_data.at(player_id).username << " has changed their username to " << username << std::endl;
                    player_data.at(player_id).username = username;
                    break;
                }
                case SetPlayerAttributesTypes::ColorChanged: {
                    Color c {data_without_type[2], data_without_type[3], data_without_type[4], data_without_type[5]};
                    ss << player_data.at(player_id).username << " has changed their color";
                    player_data.at(player_id).color = c;
                    std::cout << "Set color of " << (int)player_id << " to: (" << (int)c.r << ", " << (int)c.g << ", " << (int)c.b << ", " << (int)c.a << ")\n";
                    break;
                }
                default:
                    std::cerr << "Attribute type not recognized: " << (int)attribute_type << std::endl;
            }
            return ss.str();
            break;
        }
        case MessageToClientTypes::PlayerWon: {
            assert(data_without_type.size() == 1);
            std::cout << "Player " << (int)data_without_type[0] << " has won!" << std::endl;
            std::string text = player_data[data_without_type[0]].username + " has won!";
            return text;
            break;
        }
        case MessageToClientTypes::NewPowerUpSpawned: {
            assert(data_without_type.size() == 5);
            std::array<uint8_t, 5> data;
            for (unsigned idx = 0; idx < data_without_type.size(); idx++)
                data.at(idx) = data_without_type.at(idx);

            NewPowerUp p = deserialize_new_powerup(data);

            std::stringstream s;
            s << "New Powerup Spawned at (" << p.x << ", " << p.y << ")\nIt gives the ";
            switch (p.powerup) {
                case PowerUp::LongRangeProjectiles:
                    s << "LongRangeProjectiles";
                    break;
                case PowerUp::PhaseThroughObstacles:
                    s << "PhaseThroughObstacles";
                    break;
                case PowerUp::Speed:
                    s << "Speed";
                    break;
            }
            s << " ability\n";

            powerups.push_back(p);
            return s.str();
            break;
        }
        case MessageToClientTypes::PowerUpsClaimed: {
            uint8_t n_claimed = *data_without_type.start;
            for (auto idx = 0u; idx < n_claimed; idx++) {
                auto id = 1 + idx * (sizeof(NewPowerUp) + 1);

                auto start = std::next(data_without_type.start, id + 1);
                auto end = std::next(start, sizeof(NewPowerUp) - 1);
                std::array<uint8_t, 5> data;
                auto dit = data.begin();
                for (auto it = start; it != end; it++) {
                    *dit = *it;
                    dit++;
                }

                [[maybe_unused]] auto player_id = *std::prev(start);

                NewPowerUp powerup = deserialize_new_powerup(data);
                auto it = std::find_if(powerups.begin(), powerups.end(), [powerup](const auto p) { return p.x == powerup.x && p.y == powerup.y; });
                if (it != powerups.end())
                    powerups.erase(it);
            }
        }
        case MessageToClientTypes::PreviousGameData:
            break; // previous game data is handled in connect_to_server() function
    }
    return "";
}

template <typename T>
void send_packet(ENetPeer *peer, const T &data, int channel_id) {
    ENetPacket *packet = enet_packet_create(data.data(), data.size(), ENET_PACKET_FLAG_RELIABLE);
    int val = enet_peer_send(peer, channel_id, packet);
    if (val != 0) {
        std::cerr << "Failed to send packet: " << val << std::endl;
        enet_packet_destroy(packet);
    }
}

// gets the player that is this client
Player get_this_player(ENetHost *client) {
    ENetEvent event;
    int err = enet_host_service(client, &event, 800);
    if (err < 0) {
        std::cerr << "Failed to get player data: " << err << std::endl;
        std::exit(1);
    }
    Player this_player;
    if (event.type == ENET_EVENT_TYPE_RECEIVE) {
        IteratorRange vec {event.packet->data + 1, event.packet->data + event.packet->dataLength};
        std::cout << "Data received: " << *(int*)event.packet->data << " and " << vec << std::endl;
        this_player = deserialize_player(vec);
        std::cout << "Received player: " << this_player.username << ", ("
                  << this_player.x << ", " << this_player.y << "), (" << (int)this_player.color.r << ','
                  << (int)this_player.color.g << ',' << (int)this_player.color.b << ',' << (int)this_player.color.a << "):"
                  << (int)this_player.id << std::endl;
    } else {
        std::cerr << "Did not receive player data: (ENetEventType)" << event.type << std::endl;
        std::exit(1);
    }
    return this_player;
}

std::pair<std::map<int, Player>, std::vector<Obstacle>> get_previous_game_data(ENetHost *client) {
    ENetEvent event;
    int err = enet_host_service(client, &event, 800);
    if (err < 0) {
        std::cerr << "Failed to get player data: " << err << std::endl;
        std::exit(1);
    }
    if (event.type == ENET_EVENT_TYPE_RECEIVE) {
        IteratorRange vec {event.packet->data + 1, event.packet->data + event.packet->dataLength};
        std::cout << "Data received: " << *(int*)event.packet->data << " and " << vec << std::endl;
        auto [previous_players, obstacles] = deserialize_and_update_previous_game_data(vec);
        std::cout << "Received " << previous_players.size() << " player(s) and " << obstacles.size() << " obstacle(s)" << std::endl;
        return std::make_pair(previous_players, obstacles);
    } else {
        std::cerr << "Did not receive previous game data: " << event.type << std::endl;
        std::exit(1);
    }
}

std::tuple<Player, std::map<int, Player>, std::vector<Obstacle>, ENetPeer*> connect_to_server(ENetHost *client, const std::string &server_addr, int port) {
    ENetAddress address;
    ENetEvent enet_event;
    
    enet_address_set_host(&address, server_addr.c_str());
    address.port = port;
    ENetPeer *server {enet_host_connect(client, &address, num_channels, 0)};
    if (server == NULL) {
        std::cerr << "Failed to connect to peer" << std::endl;
        std::exit(1);
    }

    if (enet_host_service(client, &enet_event, 5000) > 0 && enet_event.type == ENET_EVENT_TYPE_CONNECT) {
        std::cout << "Connection to " << server_addr << ":" << address.port << " success" << std::endl;
    } else {
        enet_peer_reset(server);
        std::cout << "Connection to " << server_addr << ":" << address.port << " failed" << std::endl;
    }

    // get player data
    Player this_player = get_this_player(client);

    // get previous game data
    auto [players, obstacles] = get_previous_game_data(client);
    return {this_player, players, obstacles, server};
}

struct NetworkingPacket {
    std::vector<uint8_t> data;

    enum class State: uint8_t {
        None,
        ClientClosed,
        ServerDisconnected,
    };
    State state = State::None;
    short channel = channel_updates;
};

void networking(
    ENetHost* client,
    ENetPeer* server,
    ThreadSafeQueue<NetworkingPacket>& inbound,
    ThreadSafeQueue<NetworkingPacket>& outbound
) {
    using namespace std::chrono_literals;

    bool running = true;

    while (running) {
        std::optional<NetworkingPacket> msg = outbound.try_pop();
        while (msg.has_value()) {
            if (msg->state == NetworkingPacket::State::ClientClosed) {
                running = false;
                break;
            }

            send_packet(server, msg->data, msg->channel);
            msg = outbound.try_pop();
        }

        ENetEvent enet_event;
        while (enet_host_service(client, &enet_event, 0) > 0) {
            switch (enet_event.type) {
                case ENET_EVENT_TYPE_RECEIVE: {
                    std::vector<uint8_t> data;
                    for (int i{0}; i < enet_event.packet->dataLength; i++)
                        data.push_back(enet_event.packet->data[i]);
                    std::cout << "Received data: " << data << " on channel: " << (int)enet_event.channelID << '\n';
                    inbound.push({std::move(data), NetworkingPacket::State::None, enet_event.channelID});
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT: {
                    inbound.push({{}, NetworkingPacket::State::ServerDisconnected, enet_event.channelID});
                    running = false;
                }
                default:
                    break;
            }
        }
        std::this_thread::sleep_for(1ms);

    }
}

int main(int argv, char **argc) {
    if (enet_initialize() != 0) {
        std::cerr << "An error occurred while initializing Enet!" << std::endl;
        return 1;
    }

    ENetHost *client {enet_host_create(NULL, 1, num_channels, 0, 0)};
    if (client == NULL) {
        std::cerr << "An error occured while creating ENetHost" << std::endl;
        return EXIT_FAILURE;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL failed to initialize: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window *window {nullptr};
    SDL_Renderer *renderer {nullptr};
    SDL_CreateWindowAndRenderer(window_title.c_str(), window_size, window_height, SDL_WINDOW_HIGH_PIXEL_DENSITY, &window, &renderer);

    if (!window || window == NULL) {
        std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
        return 1;
    }

    if (!renderer || renderer == NULL) {
        std::cerr << "Failed to create renderer: " << SDL_GetError() << std::endl;
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io {ImGui::GetIO()};
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->AddFontFromFileTTF("fonts/Rubik-Regular.ttf", 16.0f);

    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    io.Fonts->AddFontFromFileTTF("fonts/Font Awesome 7 Free-Solid-900.otf", 0.0f, &icons_config);

    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    std::atexit([](){
        ImGui_ImplSDL3_Shutdown();
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui::DestroyContext();
        enet_deinitialize();
        SDL_Quit();
    });

    std::string server_addr;
    int port {};

    for (int i {0}; i < argv; i++)
        std::cout << argc[i] << std::endl;

    if (argv == 3) {
        server_addr = argc[1];
        port = std::stoi(argc[2]);
    } else if (argv == 2)
        port = std::stoi(argc[1]);


    std::pair<short, short> player_movement;
    std::vector<Projectile> projectiles;
    std::vector<Particle> particles;
    std::vector<NewPowerUp> powerups;
    auto last_time = SDL_GetTicks();
    ImVec4 player_color {1.0f, 1.0f, 1.0f, 1.0f};
    char username[15] = "";

    #ifdef DEBUG
    // this is so that I don't have to input these things again and again
    // while developing the game

    // random 3 letter username
    username[0] = SDL_rand(26) + 'a';
    username[1] = SDL_rand(26) + 'a';
    username[2] = SDL_rand(26) + 'a';

    // random color
    player_color.x = SDL_rand(256) / 256.0f;
    player_color.y = SDL_rand(256) / 256.0f;
    player_color.z = SDL_rand(256) / 256.0f;

    server_addr = "localhost";
    port = 8888;
    #endif

    bool connected_to_server = false;
    bool game_started = false;
    bool is_ready = false;
    std::pair<bool, std::string> player_won {false, ""};

    ThreadSafeQueue<NetworkingPacket> inbound_queue, outbound_queue;
    
    std::string latest_event;
    auto latest_event_time = SDL_GetTicks();

    // the player the client is controlling/spectating
    uint8_t local_player_id {};
    std::map<int, Player> players;
    ENetPeer *server {nullptr};
    std::vector<Obstacle> obstacles;

    bool running = true;
    SDL_Event event;
    std::thread networking_thread;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    auto cleanup = [&]() {
        game_started = false;
        connected_to_server = false;
        player_won = {false, ""};

        players.clear();
        obstacles.clear();
        projectiles.clear();
        particles.clear();
        powerups.clear();

        local_player_id = 0;
        spectating = false;

        outbound_queue.push({{}, NetworkingPacket::State::ClientClosed, channel_updates});
        enet_peer_reset(server);
        networking_thread.join();

        inbound_queue.reset();
        outbound_queue.reset();
    };

    while (running) {
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                running = false;
            else if (connected_to_server && !spectating) {
                auto last_movement = player_movement;
                std::vector<uint8_t> data_to_send {process_event(event, player_movement, players.at(local_player_id))};
                if (!data_to_send.empty() && (player_movement != last_movement || event.type == SDL_EVENT_MOUSE_BUTTON_UP)) {
                    outbound_queue.push({std::move(data_to_send), NetworkingPacket::State::None});
                }
            }
        }
        
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if (!connected_to_server) {
            ImGui::Begin("Enter your details", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
            ImGui::ColorEdit4("Choose your color", (float*)&player_color);
            ImGui::InputTextWithHint("Username", "Enter username", username, 15);
            ImGui::InputTextWithHint("Input server address", "Enter server address", &server_addr);
            ImGui::InputInt("Input server port", &port);

            if (ImGui::Button("Connect to the server")) {
                connected_to_server = true;
                Player local_player;
                std::tie(local_player, players, obstacles, server) = connect_to_server(client, server_addr, port);
                players[local_player.id] = local_player;
                local_player_id = local_player.id;
                // send username and color to server
                std::vector<uint8_t> color_change {
                    static_cast<uint8_t>(MessageToServerTypes::SetClientAttributes),
                    static_cast<uint8_t>(SetPlayerAttributesTypes::ColorChanged),
                    static_cast<uint8_t>(player_color.x * 255),
                    static_cast<uint8_t>(player_color.y * 255),
                    static_cast<uint8_t>(player_color.z * 255),
                    static_cast<uint8_t>(player_color.w * 255)
                };
                send_packet(server, color_change, channel_user_updates);
                std::vector<uint8_t> username_change {
                    static_cast<uint8_t>(MessageToServerTypes::SetClientAttributes),
                    static_cast<uint8_t>(SetPlayerAttributesTypes::UsernameChanged),
                    static_cast<uint8_t>(strlen(username))
                };
                username_change.insert(username_change.end(), username, username + strlen(username));
                send_packet(server, username_change, channel_user_updates);

                std::thread n_thread([&]() {
                    networking(client, server, inbound_queue, outbound_queue);
                });
                networking_thread.swap(n_thread);
            }
            ImGui::End();
        } else {
            std::optional<NetworkingPacket> data_received;
            while ((data_received = inbound_queue.try_pop()).has_value()) {
                if (data_received->state == NetworkingPacket::State::ServerDisconnected)
                    cleanup();
                else {
                    std::string new_event = parse_message_from_server(data_received->data, players, projectiles, particles, powerups, local_player_id, window);
                    if (new_event != "") {
                        latest_event = new_event;
                        latest_event_time = SDL_GetTicks();
                    }
                    if (new_event == "The game has started!")
                        game_started = true;
                    if (new_event == restart_client)
                        cleanup();
                    if (data_received->data.at(0) == (uint8_t)MessageToClientTypes::PlayerWon) {
                        player_won = {true, players.at(data_received->data.at(1)).username};

                        // add a lot of explosions (otherwise known as particles)
                        auto new_particles = create_particles<10>(players.at(data_received->data.at(1)).x / 2 + player_size, players.at(data_received->data.at(1)).y / 2 + player_size, 100);
                        particles.insert(particles.end(), new_particles.begin(), new_particles.end());
                        new_particles = create_particles<10>(0, 0, 50);
                        particles.insert(particles.end(), new_particles.begin(), new_particles.end());
                        new_particles = create_particles<10>(window_size, window_size, 50);
                        particles.insert(particles.end(), new_particles.begin(), new_particles.end());
                        new_particles = create_particles<10>(window_size, 0, 50);
                        particles.insert(particles.end(), new_particles.begin(), new_particles.end());
                        new_particles = create_particles<10>(0, window_size, 50);
                        particles.insert(particles.end(), new_particles.begin(), new_particles.end());
                    }
                }
            }
            ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
            ImGui::Text("Frame time: %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
            ImGui::Begin("Events", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
            if (SDL_GetTicks() - latest_event_time < 5000)
                ImGui::Text("%s", latest_event.c_str());
            ImGui::End();
            if (!game_started) {
                ImGui::SetNextWindowPos(ImVec2(window_size / 2.0, 0));
                ImGui::SetNextWindowSize(ImVec2(300, 80));
                ImGui::Begin("Waiting for game to start", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
                if (!is_ready && ImGui::Button("Ready to play?")) {
                    outbound_queue.push({{static_cast<uint8_t>(MessageToServerTypes::ReadyUp)}, NetworkingPacket::State::None, channel_user_updates});
                    is_ready = true;
                } else if (is_ready) {
                    ImGui::Text("Waiting for other players to be ready...");
                    if (ImGui::Button("Unready")) {
                        outbound_queue.push({{static_cast<uint8_t>(MessageToServerTypes::UnReady)}, NetworkingPacket::State::None, channel_user_updates});
                        is_ready = false;
                    }
                }
                ImGui::End();
            } else if (player_won.first) {
                ImGui::Begin("#1 Victory Royale", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
                ImGui::Text("%s won!", player_won.second.c_str());
                ImGui::End();
            } else if (spectating) {
                using std::prev, std::next;

                ImGui::Begin("Spectating", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
                bool can_go_previous = players.find(local_player_id) != players.begin();
                bool can_go_next = players.find(local_player_id) != prev(players.end());
                
                if (!can_go_previous)
                    ImGui::BeginDisabled();
                if (ImGui::Button(LEFT_ARROW)) {
                    std::cout << "spectating previous player\n";
                    local_player_id = prev(players.find(local_player_id))->first;
                }
                if (!can_go_previous)
                    ImGui::EndDisabled();

                ImGui::SameLine();
                ImGui::Text("Spectating %s", players.at(local_player_id).username.c_str());
                ImGui::SameLine();

                if (!can_go_next)
                    ImGui::BeginDisabled();
                if (ImGui::Button(RIGHT_ARROW)) {
                    std::cout << "spectating next player\n";
                    local_player_id = next(players.find(local_player_id))->first;
                }
                if (!can_go_next)
                    ImGui::EndDisabled();
                ImGui::End();
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        for (const auto &[id, player] : players) {
            if (id == local_player_id) {
                draw_this_player(renderer, players.at(local_player_id));
            } else {
                draw_player(renderer, player, players.at(local_player_id));
                draw_player_username(player, players.at(local_player_id));
            }
        }
        
        for (const auto &obstacle : obstacles)
            draw_obstacle(renderer, obstacle, players.at(local_player_id));

        for (auto p : projectiles)
            draw_projectile(renderer, p, players.at(local_player_id));

        for (auto p : powerups)
            draw_powerup(renderer, p, players.at(local_player_id));


        update_particles(particles);
        draw_particles(renderer, particles);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderLine(renderer, 0, window_size, window_size, window_size);

        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);
        auto elapsed = SDL_GetTicks() - last_time;
        if (elapsed < tick_rate_ms)
            SDL_Delay(tick_rate_ms - elapsed);
        last_time = SDL_GetTicks();
    }

    outbound_queue.push({{}, NetworkingPacket::State::ClientClosed, channel_updates});
    if (networking_thread.joinable())
        networking_thread.join();
    enet_peer_disconnect(server, 0);
    ENetEvent enet_event;
    while (enet_host_service(client, &enet_event, 500) > 0) {
        switch (enet_event.type) {
            case ENET_EVENT_TYPE_RECEIVE:
                enet_packet_destroy(enet_event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                std::cout << "Disconnet event received" << std::endl;
                break;
            default:
                break;
        }
    }

    std::cout << "Disconnected from server" << std::endl;
    return 0;
}
