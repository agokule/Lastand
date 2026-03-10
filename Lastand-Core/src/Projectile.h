#pragma once
#ifndef PROJECTILE_H
#define PROJECTILE_H
#include <cstdint>

struct ClientProjectile {
    uint16_t x;
    uint16_t y;
    int32_t dx; // either 1 or -1
    int32_t dy;
};

struct Projectile {
    uint16_t x;
    uint16_t y;
};

#endif // PROJECTILE_H
