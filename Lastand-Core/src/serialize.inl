#pragma once
#define IN_INLFILE
#include "serialize.h"
#undef IN_INLFILE

#include <iostream>

template<class InputIt>
Player deserialize_player(InputIt start, InputIt end) {
    using std::next;

    if (std::distance(start, end) <= 11) {
        std::cerr << "Not enough data to deserialize a player, data: " << IteratorRange{start, end} << std::endl;
        throw std::runtime_error("Not enough data to deserialize a player");
    }

    Player p {*(start + 4)}; // data[4] is the player id
    uint16_t x = deserialize_uint16(*start, *next(start));
    uint16_t y = deserialize_uint16(*next(start, 2), *next(start, 3));
    p.x = x;
    p.y = y;

    p.color = {*next(start, 5), *next(start, 6), *next(start, 7), *next(start, 8)};
    p.powerups = (PowerUp)(*next(start, 9));
    
    uint8_t username_length = *next(start, 10);
    if (username_length + 10 != std::distance(start, end) - 1) {
        std::cerr << "Warning: Username length mismatch: " << (int)username_length + 10 << " vs " << std::distance(start, end) - 1 << std::endl;
    }
    std::string username;
    for (auto it = next(start, 11); it != end; it++) {
        username.push_back(*it);
    }
    p.username = username;
    return p;
}

template<class InputIt>
Player deserialize_player(IteratorRange<InputIt> data) {
    return deserialize_player(data.start, data.end);
}

template <class InputIt>
Obstacle deserialize_obstacle(IteratorRange<InputIt> data) {
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

template <typename InputIt>
void deserialize_and_update_game_player_positions(IteratorRange<InputIt> data, std::map<int, Player> &players) {
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

template <typename InputIt>
ClientProjectile deserialize_client_projectile(IteratorRange<InputIt> data) {
    if (data.size() != 12)
        std::cerr << "Client projectile data size is not equal to 12, data: " << data << '\n';
    auto x = deserialize_uint16(data[0], data[1]);
    auto y = deserialize_uint16(data[2], data[3]);

    auto dx = deserialize_int32({data[4], data[5], data[6], data[7]});
    auto dy = deserialize_int32({data[8], data[9], data[10], data[11]});

    return {x, y, dx, dy};
}


template <class InputIt>
std::pair<std::map<int, Player>, std::vector<Obstacle>> deserialize_and_update_previous_game_data(IteratorRange<InputIt> data) {
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
        uint8_t username_length = data[curr_data_idx + 10];

        auto player_data_begin = data.start;
        std::advance(player_data_begin, curr_data_idx);

        auto player_data_end = data.start;
        std::advance(player_data_end, curr_data_idx + 11 + username_length);

        IteratorRange player_data {player_data_begin, player_data_end};
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
        auto obstacle_data_begin = std::next(data.start, curr_data_idx);
        auto obstacle_data_end = std::next(data.start, curr_data_idx + obstacle_data_size);

        IteratorRange obstacle_data {obstacle_data_begin, obstacle_data_end};

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
