#include "PowerUps.h"
#include <cstdint>

PowerUp operator|(PowerUp p1, PowerUp p2) {
    return PowerUp((uint8_t)p1 | (uint8_t)p2);
}

PowerUp operator|=(PowerUp& p1, PowerUp p2) {
    p1 = p1 | p2;
    return p1;
}

bool operator&(PowerUp p1, PowerUp p2) {
    return (uint8_t)p1 & (uint8_t) p2;
}

