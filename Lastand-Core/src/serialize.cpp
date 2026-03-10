#include "serialize.h"
#include "Projectile.h"
#include <array>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif // _WIN32

std::pair<uint8_t, uint8_t> serialize_uint16(uint16_t val) {
    uint16_t network_value = htons(val); // Convert to network byte order

    uint8_t high_byte = static_cast<uint8_t>((network_value & 0xFF00) >> 8);
    uint8_t low_byte = static_cast<uint8_t>(network_value & 0x00FF);

    return std::make_pair(high_byte, low_byte);
}

uint16_t deserialize_uint16(uint8_t high_byte, uint8_t low_byte) {
    uint16_t network_value = (high_byte << 8) | low_byte;
    return ntohs(network_value);
}

std::array<uint8_t, 4> serialize_int32(int32_t val) {
    uint32_t network_value = htonl(val); // Convert to network byte order

    uint16_t high_byte = static_cast<uint16_t>((network_value & 0xFFFF0000) >> 16);
    uint16_t low_byte = static_cast<uint16_t>(network_value & 0x0000FFFF);

    auto [b1, b2] = serialize_uint16(high_byte);
    auto [b3, b4] = serialize_uint16(low_byte);
    return {b1, b2, b3, b4};
}

int32_t deserialize_int32(std::array<uint8_t, 4> data) {
    uint16_t high_byte = deserialize_uint16(data[0], data[1]);
    uint16_t low_byte = deserialize_uint16(data[2], data[3]);

    int32_t network_value = (high_byte << 16) | low_byte;
    return ntohl(network_value);
}

std::array<uint8_t, 4> serialize_color(Color color) {
    std::array<uint8_t, 4> result;
    result[0] = color.r;
    result[1] = color.g;
    result[2] = color.b;
    result[3] = color.a;
    return result;
}

std::array<uint8_t, 4> serialize_coordinates(uint16_t x, uint16_t y) {
    std::array<uint8_t, 4> result;
    auto player_x {serialize_uint16(x)};
    auto player_y {serialize_uint16(y)};

    result[0] = player_x.first;
    result[1] = player_x.second;
    result[2] = player_y.first;
    result[3] = player_y.second;
    return result;
}

std::vector<uint8_t> serialize_player(const Player &player) {
    std::vector<uint8_t> result;
    result.reserve(sizeof(Player));

    auto coordinates {serialize_coordinates(player.x, player.y)};
    for (auto c : coordinates) {
        result.push_back(c);
    }

    result.push_back(player.id);

    auto color {serialize_color(player.color)};
    for (auto c : color) {
        result.push_back(c);
    }

    if (player.username.size() > 255) {
        std::cerr << "Username too long to serialize! username: " << player.username << std::endl;
        throw std::runtime_error("Username is too long to serialize!");
    }
    uint8_t username_length = static_cast<uint8_t>(player.username.size());
    result.push_back(username_length);
    for (uint8_t c: player.username) {
        result.push_back(c);
    }

    return result;
}

Player deserialize_player(const std::vector<uint8_t> &data) {
    if (data.size() <= 10) {
        std::cerr << "Not enough data to deserialize a player, data: " << data << std::endl;
        throw std::runtime_error("Not enough data to deserialize a player");
    }

    Player p {data[4]}; // data[4] is the player id
    uint16_t x = deserialize_uint16(data[0], data[1]);
    uint16_t y = deserialize_uint16(data[2], data[3]);
    p.x = x;
    p.y = y;

    p.color = {data[5], data[6], data[7], data[8]};
    
    uint8_t username_length = data[9];
    if (username_length + 9 != data.size() - 1) {
        std::cerr << "Warning: Username length mismatch: " << (int)username_length + 9 << " vs " << data.size() - 1 << std::endl;
    }
    std::string username;
    for (size_t i {10}; i < data.size(); i++) {
        username.push_back(data[i]);
    }
    p.username = username;
    return p;
}


std::array<uint8_t, 12> serialize_obstacle(const Obstacle &obstacle) {
    std::array<uint8_t, 12> result;

    auto [high_byte, low_byte] = serialize_uint16(obstacle.x);
    result[0] = high_byte;
    result[1] = low_byte;

    std::tie(high_byte, low_byte) = serialize_uint16(obstacle.y);
    result[2] = high_byte;
    result[3] = low_byte;

    std::tie(high_byte, low_byte) = serialize_uint16(obstacle.width);
    result[4] = high_byte;
    result[5] = low_byte;

    std::tie(high_byte, low_byte) = serialize_uint16(obstacle.height);
    result[6] = high_byte;
    result[7] = low_byte;

    auto color_data = serialize_color(obstacle.color);
    result[8] = color_data[0];
    result[9] = color_data[1];
    result[10] = color_data[2];
    result[11] = color_data[3];

    return result;
}


Obstacle deserialize_obstacle(const std::array<uint8_t, 12> &data) {
    Obstacle result;

    uint8_t high_byte = data[0];
    uint8_t low_byte = data[1];
    result.x = deserialize_uint16(high_byte, low_byte);

    high_byte = data[2];
    low_byte = data[3];
    result.y = deserialize_uint16(high_byte, low_byte);

    high_byte = data[4];
    low_byte = data[5];
    result.width = deserialize_uint16(high_byte, low_byte);

    high_byte = data[6];
    low_byte = data[7];
    result.height = deserialize_uint16(high_byte, low_byte);

    result.color.r = data[8];
    result.color.g = data[9];
    result.color.b = data[10];
    result.color.a = data[11];

    return result;
}

void update_player_delta(ClientMovement movement, bool key_up, std::pair<short, short> &player_delta) {
    auto m = static_cast<uint8_t>(movement);
    if (!key_up) {
        // key is currently being held down
        if (m & (uint8_t)ClientMovement::Left)
            player_delta.first = -1;
        else if (m & (uint8_t)ClientMovement::Right)
            player_delta.first = 1;
        
        if (m & (uint8_t)ClientMovement::Up)
            player_delta.second = -1;
        else if (m & (uint8_t)ClientMovement::Down)
            player_delta.second = 1;
    } else {
        if (m & (uint8_t)ClientMovement::Right || m & (uint8_t)ClientMovement::Left)
            player_delta.first = 0;

        if (m & (uint8_t)ClientMovement::Up || m & (uint8_t)ClientMovement::Down)
            player_delta.second = 0;
    }
}

// takes in a vector of players that were updated by the server and serializes them
std::vector<uint8_t> serialize_game_player_positions(const std::vector<Player> &players) {
    std::vector<uint8_t> result;
    result.reserve(players.size() * 5 + 1);
    if (players.size() > 255) {
        std::cerr << "Player vector too big to serialize!" << players.size() << std::endl;
        return {};
    }
    result.push_back(static_cast<uint8_t>(players.size()));
    for (const auto &p: players) {
        result.push_back(p.id);
        auto coordinates {serialize_coordinates(p.x, p.y)};
        for (auto c : coordinates) {
            result.push_back(c);
        }
    }
    return result;
}

void deserialize_and_update_game_player_positions(const std::vector<uint8_t> &data, std::map<int, Player> &players) {
    if (data.size() < 1) return;
    uint8_t num_players = data[0];
    if (data.size() != num_players * 5 + 1) {
        std::cerr << "Not enough data to deserialize players, data:" << data << std::endl;
        return;
    }

    for (size_t curr_player = 1; curr_player <= data.size() - 5; curr_player += 5) {
        int id = data[curr_player];
        auto &p = players[id];
        uint16_t x = deserialize_uint16(data[curr_player + 1], data[curr_player + 2]);
        uint16_t y = deserialize_uint16(data[curr_player + 3], data[curr_player + 4]);
        p.x = x;
        p.y = y;
    }
}

std::vector<uint8_t> serialize_previous_game_data(const std::vector<Player> &players, const std::vector<Obstacle> &obstacles) {
    std::vector<uint8_t> player_data;
    for (const auto &p : players) {
        auto player = serialize_player(p);
        player_data.insert(player_data.end(), player.begin(), player.end());
    }
    std::vector<uint8_t> obstacle_data;
    std::cout << "obstacles size: " << obstacles.size() << std::endl;
    int obstacle_index {0};
    for (const auto &obs: obstacles) {
        auto data = serialize_obstacle(obs);
        obstacle_data.insert(obstacle_data.cend(), data.begin(), data.end());
        obstacle_index++;
#ifdef DEBUG
        if (obstacle_index % 10 == 0) {
            std::cout << "Processed obstacle " << obstacle_index << std::endl;
        }
#endif
    }

    std::cout << "Player data size: " << player_data.size() << std::endl;
    std::cout << "Obstacle data size: " << obstacle_data.size() << std::endl;

    if (player_data.empty() && obstacle_data.empty()) {
        std::cout << "Not sending previous game data as it is empty" << std::endl;
        return {};
    }

    std::vector<uint8_t> previous_game_data {
        static_cast<uint8_t>(ObjectType::Player),
        static_cast<uint8_t>(players.size()),
    };
    previous_game_data.reserve(player_data.size() + obstacle_data.size() + 7);
    previous_game_data.insert(previous_game_data.cend(), player_data.begin(), player_data.end());

    previous_game_data.push_back(static_cast<uint8_t>(ObjectType::Obstacle));
    previous_game_data.push_back(static_cast<uint8_t>(obstacles.size()));
    previous_game_data.insert(previous_game_data.cend(), obstacle_data.begin(), obstacle_data.end());

    std::cout << "Previous game data size: " << previous_game_data.size() << std::endl;
    return previous_game_data;
}

std::pair<std::map<int, Player>, std::vector<Obstacle>> deserialize_and_update_previous_game_data(const std::vector<uint8_t> &data) {
    std::map<int, Player> players;
    std::vector<Obstacle> obstacles;

#ifdef DEBUG
    std::cout << "Parsing Previous game data: " << data << std::endl;
#endif
    
    ObjectType type = static_cast<ObjectType>(data[0]);
    if (type != ObjectType::Player) {
        std::cerr << "Previous game data does not have player data!" << std::endl;
        return {players, obstacles};
    }

    uint8_t num_players = data[1];
    std::cout << "Player count: " << (int)num_players << std::endl;
    int curr_data_idx {2};
    for (size_t curr_player = 0; curr_player < num_players; curr_player++) {
        uint8_t username_length = data[curr_data_idx + 9];

        auto player_data_begin = data.begin();
        std::advance(player_data_begin, curr_data_idx);

        auto player_data_end = data.begin();
        std::advance(player_data_end, curr_data_idx + 10 + username_length);

        std::vector<uint8_t> player_data(player_data_begin, player_data_end);
        Player p {deserialize_player(player_data)};
#ifdef DEBUG
        std::cout << "Player: " << p.username << ": " << "(" << p.x << ", " << p.y << ")(" << p.color.r << ", " << p.color.g << ", " << p.color.b << ", " << p.color.a << ")\n";
        std::cout << "Player data: " << player_data << std::endl;
#endif

        players[p.id] = p;
        curr_data_idx += player_data.size();
    }
    
    type = static_cast<ObjectType>(data[curr_data_idx]);
    if (type != ObjectType::Obstacle) {
        std::cerr << "Previous game data does not have obstacle data! " << (int)type << ", " << curr_data_idx << std::endl;
        return {players, obstacles};
    }

    curr_data_idx++;
    int num_obstacles = data[curr_data_idx];
    std::cout << "Parsing " << num_obstacles << " obstacles" << std::endl;
    curr_data_idx++;
    for (size_t curr_obstacle = 0; curr_obstacle < num_obstacles; curr_obstacle++) {
        auto obstacle_data_begin = data.begin();
        std::advance(obstacle_data_begin, curr_data_idx);

        auto obstacle_data_end = data.begin();
        std::advance(obstacle_data_end, curr_data_idx + obstacle_data_size);

        std::array<uint8_t, obstacle_data_size> obstacle_data;
        int i {0};
        for (auto elm = obstacle_data_begin; elm != obstacle_data_end; elm++) {
            obstacle_data[i] = *elm;
            i++;
        }
#ifdef DEBUG
        std::cout << "Obstacle data(" << obstacle_data.size() << "): " << obstacle_data << std::endl;
#endif
        Obstacle o {deserialize_obstacle(obstacle_data)};

        obstacles.push_back(o);
        curr_data_idx += obstacle_data_size;
    }

    std::cout << "curr_data_idx: " << curr_data_idx << std::endl;
    if (curr_data_idx != data.size()) {
        std::cerr << "Previous game data is not fully parsed! " << data.size() << std::endl;
    }

    return {players, obstacles};
}

ClientMovement operator|(ClientMovement c1, ClientMovement c2) {
    return ClientMovement((uint8_t)c1 | (uint8_t)c2);
}

ClientMovement operator|=(ClientMovement &c1, ClientMovement c2) {
    c1 = c1 | c2;
    return c1;
}

bool operator&(ClientMovement c1, ClientMovement c2) {
    return (uint8_t)c1 & (uint8_t)c2;
}

std::array<uint8_t, 12> serialize_client_projectile(ClientProjectile p) {
    std::array<uint8_t, 12> result;

    // testing serializing int32
    auto [high_byte, low_byte] = serialize_uint16(p.x);
    result[0] = high_byte;
    result[1] = low_byte;
    
    std::tie(high_byte, low_byte) = serialize_uint16(p.y);
    result[2] = high_byte;
    result[3] = low_byte;

    {
        auto [b1, b2, b3, b4] = serialize_int32(p.dx);
        result[4] = b1;
        result[5] = b2;
        result[6] = b3;
        result[7] = b4;
    }

    {
        auto [b1, b2, b3, b4] = serialize_int32(p.dy);
        result[8] = b1;
        result[9] = b2;
        result[10] = b3;
        result[11] = b4;
    }

    return result;
}

ClientProjectile deserialize_client_projectile(const std::array<uint8_t, 12> &data) {
    auto x = deserialize_uint16(data[0], data[1]);
    auto y = deserialize_uint16(data[2], data[3]);

    auto dx = deserialize_int32({data[4], data[5], data[6], data[7]});
    auto dy = deserialize_int32({data[8], data[9], data[10], data[11]});

    return {x, y, dx, dy};
}

std::array<uint8_t, 4> serialize_projectile(Projectile p) {
    std::array<uint8_t, 4> result;

    // testing serializing int32
    auto [high_byte, low_byte] = serialize_uint16(p.x);
    result[0] = high_byte;
    result[1] = low_byte;
    
    std::tie(high_byte, low_byte) = serialize_uint16(p.y);
    result[2] = high_byte;
    result[3] = low_byte;

    return result;
}

Projectile deserialize_projectile(const std::array<uint8_t, 4> &data) {
    auto x = deserialize_uint16(data[0], data[1]);
    auto y = deserialize_uint16(data[2], data[3]);

    return {x, y};
}

