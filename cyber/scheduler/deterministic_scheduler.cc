/******************************************************************************
 * deterministic_scheduler.cc — Deterministic CRoutine ordering implementation
 *
 * PRD #25: Run-to-run deterministic tick execution.
 *****************************************************************************/

#include "cyber/scheduler/deterministic_scheduler.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace world {
namespace cyber {
namespace scheduler {

namespace {
uint64_t NowNs() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}
}  // namespace

DeterministicScheduler::DeterministicScheduler(DeterministicConfig cfg)
    : cfg_(cfg) {}

uint64_t DeterministicScheduler::HashCombine(uint64_t a, uint64_t b) {
  // SplitMix64 finalizer — deterministic, portable, no platform dependency
  uint64_t z = a + 0x9e3779b97f4a7c15ULL + b;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

uint64_t DeterministicScheduler::ExecutionKey(WorldTick tick,
                                              uint64_t croutine_id) const {
  return HashCombine(HashCombine(cfg_.seed, tick), croutine_id);
}

void DeterministicScheduler::RunTickDeterministic(
    WorldTick tick, std::span<croutine::CRoutine*> ready_routines) {
  // Sort CRoutines by deterministic execution key
  std::sort(ready_routines.begin(), ready_routines.end(),
            [this, tick](croutine::CRoutine* a, croutine::CRoutine* b) {
              return ExecutionKey(tick, a->id()) <
                     ExecutionKey(tick, b->id());
            });

  last_sequence_.tick = tick;
  last_sequence_.execution_order.clear();
  last_sequence_.execution_order.reserve(ready_routines.size());

  auto start_ns = NowNs();

  // Execute in sorted (deterministic) order — single-threaded within the tick
  for (auto* cr : ready_routines) {
    last_sequence_.execution_order.push_back(cr->id());
    if (cr->Acquire()) {
      cr->Resume();
      cr->Release();
    }
  }

  last_sequence_.tick_duration_ns = NowNs() - start_ns;
}

void DeterministicScheduler::ReplayTick(
    const TickSequence& sequence,
    std::span<croutine::CRoutine*> routines) {
  // Build ID → CRoutine* lookup
  std::unordered_map<uint64_t, croutine::CRoutine*> by_id;
  for (auto* cr : routines) {
    by_id[cr->id()] = cr;
  }

  // Execute in the recorded order
  for (uint64_t id : sequence.execution_order) {
    auto it = by_id.find(id);
    if (it != by_id.end()) {
      auto* cr = it->second;
      if (cr->Acquire()) {
        cr->Resume();
        cr->Release();
      }
    }
  }
}

SnapshotHash DeterministicScheduler::ComputeSnapshot(
    WorldTick tick, std::span<croutine::CRoutine*> routines) const {
  // FNV-1a hash over (tick, croutine_id, croutine_state) for all routines
  // Sorted by ID for determinism
  std::vector<croutine::CRoutine*> sorted(routines.begin(), routines.end());
  std::sort(sorted.begin(), sorted.end(),
            [](auto* a, auto* b) { return a->id() < b->id(); });

  uint64_t hash = 0xcbf29ce484222325ULL;  // FNV offset basis
  auto fnv_byte = [&hash](uint8_t byte) {
    hash ^= byte;
    hash *= 0x100000001b3ULL;
  };
  auto fnv_u64 = [&fnv_byte](uint64_t v) {
    for (int i = 0; i < 8; ++i) {
      fnv_byte(static_cast<uint8_t>(v >> (i * 8)));
    }
  };

  fnv_u64(tick);
  for (auto* cr : sorted) {
    fnv_u64(cr->id());
    fnv_u64(static_cast<uint64_t>(cr->state()));
  }

  // Expand FNV-1a 64-bit to fill 32-byte SnapshotHash
  // (not cryptographic — for determinism checking, not security)
  SnapshotHash result;
  for (int i = 0; i < 4; ++i) {
    uint64_t h = HashCombine(hash, static_cast<uint64_t>(i));
    std::memcpy(result.sha256.data() + i * 8, &h, 8);
  }
  return result;
}

}  // namespace scheduler
}  // namespace cyber
}  // namespace world
