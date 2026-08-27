/******************************************************************************
 * deterministic_rng.h — Per-individual deterministic PRNG
 *
 * PRD #25: Each individual gets a deterministic RNG seeded by
 *          hash(master_seed, individual_id), with skip-ahead to any tick.
 *
 * Uses xoshiro256** — fast, high-quality, deterministic, no platform deps.
 *
 * Namespace: world::cyber::croutine
 *****************************************************************************/

#ifndef CYBER_CROUTINE_DETERMINISTIC_RNG_H_
#define CYBER_CROUTINE_DETERMINISTIC_RNG_H_

#include <array>
#include <cstdint>
#include <string>

namespace world {
namespace cyber {
namespace croutine {

/// xoshiro256** state — four 64-bit words
class Xoshiro256 {
 public:
  explicit Xoshiro256(uint64_t seed) { Seed(seed); }
  Xoshiro256() : Xoshiro256(0) {}

  void Seed(uint64_t s) {
    // SplitMix64 to fill state from a single seed
    for (int i = 0; i < 4; ++i) {
      s += 0x9e3779b97f4a7c15ULL;
      uint64_t z = s;
      z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
      z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
      state_[i] = z ^ (z >> 31);
    }
  }

  uint64_t Next() {
    const uint64_t result = Rotl(state_[1] * 5, 7) * 9;
    const uint64_t t = state_[1] << 17;
    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];
    state_[2] ^= t;
    state_[3] = Rotl(state_[3], 45);
    return result;
  }

 private:
  std::array<uint64_t, 4> state_{};

  static uint64_t Rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
  }
};

class DeterministicRng {
 public:
  DeterministicRng(uint64_t master_seed, const std::string& individual_id)
      : initial_seed_(HashCombine(master_seed, HashString(individual_id))),
        rng_(initial_seed_) {}

  /// Advance to a specific tick (skip-ahead: reseed from initial_seed + tick).
  void AdvanceToTick(uint64_t tick) {
    rng_.Seed(HashCombine(initial_seed_, tick));
  }

  uint64_t NextU64() { return rng_.Next(); }

  double NextDouble() {
    // [0.0, 1.0) with 53-bit mantissa precision
    return static_cast<double>(rng_.Next() >> 11) * 0x1.0p-53;
  }

  float NextFloat() {
    // [0.0, 1.0) with 24-bit mantissa precision
    return static_cast<float>(rng_.Next() >> 40) * 0x1.0p-24f;
  }

 private:
  uint64_t initial_seed_;
  Xoshiro256 rng_;

  static uint64_t HashCombine(uint64_t a, uint64_t b) {
    uint64_t z = a + 0x9e3779b97f4a7c15ULL + b;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  }

  static uint64_t HashString(const std::string& s) {
    // FNV-1a 64-bit
    uint64_t h = 0xcbf29ce484222325ULL;
    for (char c : s) {
      h ^= static_cast<uint64_t>(static_cast<uint8_t>(c));
      h *= 0x100000001b3ULL;
    }
    return h;
  }
};

}  // namespace croutine
}  // namespace cyber
}  // namespace world

#endif  // CYBER_CROUTINE_DETERMINISTIC_RNG_H_
