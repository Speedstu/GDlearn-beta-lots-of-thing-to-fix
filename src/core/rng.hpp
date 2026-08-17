// SPDX-License-Identifier: MIT
// Deterministic, fast, header-only PRNG (xoshiro256**).
// Every stochastic part of the project takes an explicit Rng so runs are
// bit-for-bit reproducible from a single seed.
#pragma once

#include <cmath>
#include <cstdint>

namespace gd {

class Rng {
 public:
  explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) { reseed(seed); }

  void reseed(uint64_t seed) {
    for (int i = 0; i < 4; ++i) {
      seed += 0x9E3779B97F4A7C15ull;
      uint64_t z = seed;
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
      s_[i] = z ^ (z >> 31);
    }
  }

  inline uint64_t next() {
    const uint64_t r = rotl(s_[1] * 5, 7) * 9;
    const uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return r;
  }

  // Uniform in [0,1).
  inline float uniform() {
    return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f);
  }

  // Uniform integer in [0, n).
  inline int below(int n) {
    if (n <= 1) return 0;
    return static_cast<int>(next() % static_cast<uint64_t>(n));
  }

  inline bool chance(float p) { return uniform() < p; }

  // Box-Muller, cached.
  inline float normal() {
    if (hasSpare_) {
      hasSpare_ = false;
      return spare_;
    }
    float u, v, s;
    do {
      u = uniform() * 2.0f - 1.0f;
      v = uniform() * 2.0f - 1.0f;
      s = u * u + v * v;
    } while (s >= 1.0f || s == 0.0f);
    s = std::sqrt(-2.0f * std::log(s) / s);
    spare_ = v * s;
    hasSpare_ = true;
    return u * s;
  }

 private:
  static inline uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
  }
  uint64_t s_[4]{};
  float spare_ = 0;
  bool hasSpare_ = false;
};

}  // namespace gd
