#pragma once

// much of the cpp source file was copy-pasted from the SDL source code
// and modified so it doesn't need the whole SDL libary

#include <cstdint>

void srandom(uint64_t seed);
int32_t random(int32_t n);
float randomf(void);
uint64_t random_bits(void);
uint32_t random_bits_r(uint64_t *state);
int32_t random_r(uint64_t *state, int32_t n);
float randomf_r(uint64_t *state);

