#include "core/Random.h"

namespace rr::random {

namespace {
uint32_t g_state = 1;
}

void Seed(uint32_t seed) { g_state = seed; }
uint32_t State() { return g_state; }

int16_t Rand() {
    g_state = g_state * 214013u + 2531011u;
    return int16_t((g_state >> 16) & 0x7FFF);
}

float RandRange(float low, float high) { return float(Rand()) * (1.0f / 32767.0f) * (high - low) + low; }

}  // namespace rr::random
