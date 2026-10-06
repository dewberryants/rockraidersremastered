#pragma once

#include <cstdint>

namespace rr {

// The game's single random number generator. The original's Maths_Rand and the C runtime's
// rand()/srand() share one linear congruential state (x = x * 214013 + 2531011, output bits 16..30),
// initialised to 1 and reseeded from a level's "Seed" value. Using one generator for everything
// keeps random sequences identical to the original when the same calls happen in the same order.
namespace random {

void Seed(uint32_t seed);
uint32_t State();
int16_t Rand();                        // 0..32767
float RandRange(float low, float high);  // low + Rand()/32767 * (high - low)

}  // namespace random
}  // namespace rr
