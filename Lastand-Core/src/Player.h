#pragma once
#include <cstdint>
#include "PowerUps.h"
#include "utils.h"
#include <string>

constexpr uint8_t player_size {20};

struct Player {
    uint16_t x;
    uint16_t y;

    uint8_t id;

    Color color;
    PowerUp powerups;

    std::string username;
public:
    Player(uint16_t x, uint16_t y, Color color, const std::string &username, uint8_t id): 
        x {x}, y {y}, id {id}, color {color}, powerups {0}, username {username} {};
    Player(): Player {0, 0, Color {}, "Player", 0} {};
    Player(uint8_t id): Player {0, 0, {}, "Player", id} {};
    void move(std::pair<short, short> delta);
};

