/******************************************************************************
 * deterministic_scheduler.h — Run-to-run deterministic CRoutine ordering
 *
 * PRD #25: Deterministic individual tick-loop execution ordering and
 *          coroutine state-scan sequencing for reproducible world snapshots.
 *
 * Namespace: world::cyber::scheduler
 *****************************************************************************/

#ifndef CYBER_SCHEDULER_DETERMINISTIC_SCHEDULER_H_
#define CYBER_SCHEDULER_DETERMINISTIC_SCHEDULER_H_

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cyber/croutine/croutine.h"
#include "cyber/time/time.h"

namespace world {
namespace cyber {
namespace scheduler {

using WorldTick = uint64_t;
using IndividualId = std::string;

struct DeterministicConfig {
  uint64_t seed = 0;
  bool enable_deterministic_mode = false;
  bool export_tick_sequences = false;
  bool verify_snapshot_hashes = false;
};

struct SnapshotHash {
  std::array<uint8_t, 32> sha256{};
  bool operator==(const SnapshotHash& other) const = default;
};

struct TickSequence {
  WorldTick tick = 0;
  std::vector<uint64_t> execution_order;  // CRoutine IDs in execution order
  SnapshotHash pre_tick_hash;
  SnapshotHash post_tick_hash;
  uint64_t tick_duration_ns = 0;
};

class DeterministicScheduler {
 public:
  explicit DeterministicScheduler(DeterministicConfig cfg);

  /// Generate deterministic execution key: hash(seed, tick, croutine_id)
  /// Stable across runs, platforms, and restarts given the same seed.
  uint64_t ExecutionKey(WorldTick tick, uint64_t croutine_id) const;

  /// Sort and execute CRoutines in deterministic order within a tick.
  void RunTickDeterministic(WorldTick tick,
                            std::span<croutine::CRoutine*> ready_routines);

  /// Export the execution order of the last tick.
  const TickSequence& ExportLastSequence() const { return last_sequence_; }

  /// Replay a previously exported sequence.
  void ReplayTick(const TickSequence& sequence,
                  std::span<croutine::CRoutine*> routines);

  /// Compute world snapshot hash (FNV-1a of all CRoutine state bytes).
  SnapshotHash ComputeSnapshot(
      WorldTick tick,
      std::span<croutine::CRoutine*> routines) const;

  static bool SnapshotsMatch(const SnapshotHash& a, const SnapshotHash& b) {
    return a == b;
  }

 private:
  DeterministicConfig cfg_;
  TickSequence last_sequence_;

  /// SplitMix64 hash combine — deterministic, fast, portable.
  static uint64_t HashCombine(uint64_t a, uint64_t b);
};

}  // namespace scheduler
}  // namespace cyber
}  // namespace world

#endif  // CYBER_SCHEDULER_DETERMINISTIC_SCHEDULER_H_
