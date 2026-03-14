#pragma once

#include <cstdint>

enum class PowerUp : uint8_t {
    PhaseThroughObstacles = 0b0001,
    Speed                 = 0b0010,
    LongRangeProjectiles  = 0b0100,
};

PowerUp operator|(PowerUp p1, PowerUp p2);
PowerUp operator|=(PowerUp& p1, PowerUp p2);
bool operator&(PowerUp p1, PowerUp p2);

struct NewPowerUp {
    uint16_t x;
    uint16_t y;
    PowerUp powerup;

    NewPowerUp(uint16_t x, uint16_t y, PowerUp powerup): x {x}, y {y}, powerup {powerup} {}
};

