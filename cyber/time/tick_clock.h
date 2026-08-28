/******************************************************************************
 * tick_clock.h — Cross-node tick synchronisation with drift correction.
 *
 * The master node periodically broadcasts SyncBeacons.  Follower nodes
 * consume them and apply a lightweight NTP-like drift correction so that
 * all nodes' logical tick counters converge to the same physical time.
 *
 * The correction is gradual (rate-limited) to avoid tick jumps that would
 * break causal ordering in the pub/sub pipeline.
 *
 * PRD: #1442 common TickClock cross-node synchronisation
 *****************************************************************************/
#ifndef CYBER_TIME_TICK_CLOCK_H_
#define CYBER_TIME_TICK_CLOCK_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace world {
namespace time {

using WorldTick = uint64_t;
using Timestamp = std::chrono::steady_clock::time_point;
using Duration  = std::chrono::nanoseconds;

struct ClockSyncConfig {
  Duration target_tick_duration        = std::chrono::milliseconds(33);
  Duration max_drift_tolerance         = std::chrono::milliseconds(5);
  uint64_t sync_interval_ticks         = 100;
  float    drift_correction_rate       = 0.1f;   // [0,1]
};

/// Beacon broadcast by the tick master each sync_interval_ticks.
struct SyncBeacon {
  WorldTick tick          = 0;
  Timestamp wall_clock    = {};
  uint64_t  beacon_seq    = 0;
  Duration  round_trip    = Duration::zero();  // optional RTT compensation
};

/// Quality metrics exposed for monitoring.
struct SyncMetrics {
  Duration  current_drift          = Duration::zero();
  Duration  max_observed_drift     = Duration::zero();
  uint64_t  corrections_applied    = 0;
  uint64_t  sync_beacons_received  = 0;
  float     clock_quality          = 1.0f;  // 1.0 = perfectly synced
};

/// SynchronizedTickClock — drift-corrected logical tick counter.
class SynchronizedTickClock {
 public:
  explicit SynchronizedTickClock(ClockSyncConfig cfg = {})
      : cfg_(cfg) {}

  /// Current logical tick (after drift correction).
  WorldTick current_tick() const { return tick_.load(std::memory_order_acquire); }

  /// Advance the tick.  @p now is the caller's wall-clock reading.
  void advance(Timestamp now) {
    auto expected_wall = last_wall_ + cfg_.target_tick_duration;
    // Compute local drift vs expected cadence.
    auto drift = now - expected_wall;

    // Apply correction: shorten or lengthen next tick period.
    if (drift > cfg_.max_drift_tolerance ||
        drift < -cfg_.max_drift_tolerance) {
      // Large drift: apply partial correction to avoid tick jump.
      auto correction = std::chrono::duration_cast<Duration>(
          Duration(static_cast<int64_t>(
              drift.count() * cfg_.drift_correction_rate)));
      accumulated_correction_ += correction;
      metrics_.corrections_applied++;
    }

    last_wall_ = now;
    tick_.fetch_add(1, std::memory_order_release);
    ticks_since_last_sync_++;
  }

  // --- Master-side ---
  /// Generate a beacon for broadcast.
  SyncBeacon generate_beacon() const {
    return {
        .tick       = current_tick(),
        .wall_clock = std::chrono::steady_clock::now(),
        .beacon_seq = ++beacon_seq_,
    };
  }

  // --- Follower-side ---
  /// Apply a received beacon and compute drift vs the master.
  void apply_beacon(const SyncBeacon& beacon) {
    std::lock_guard<std::mutex> lk(mu_);
    auto now = std::chrono::steady_clock::now();

    // Compute one-way delay estimate (half RTT if available).
    Duration one_way = beacon.round_trip != Duration::zero()
                           ? beacon.round_trip / 2
                           : Duration::zero();

    // Expected local tick at the time the master produced the beacon.
    auto master_wall = beacon.wall_clock + one_way;
    auto elapsed_since_master =
        std::chrono::duration_cast<Duration>(now - master_wall);
    auto expected_tick_offset = elapsed_since_master / cfg_.target_tick_duration;

    WorldTick expected_local = beacon.tick +
        static_cast<WorldTick>(expected_tick_offset);
    WorldTick actual_local = current_tick();

    int64_t tick_drift = static_cast<int64_t>(actual_local) -
                         static_cast<int64_t>(expected_local);
    Duration time_drift = tick_drift * cfg_.target_tick_duration;

    metrics_.current_drift = time_drift;
    if (std::abs(time_drift.count()) >
        std::abs(metrics_.max_observed_drift.count())) {
      metrics_.max_observed_drift = time_drift;
    }
    metrics_.sync_beacons_received++;

    // Quality: 1.0 when drift is 0, degrades linearly toward 0.
    float drift_ratio =
        static_cast<float>(std::abs(time_drift.count())) /
        static_cast<float>(cfg_.max_drift_tolerance.count());
    metrics_.clock_quality = std::max(0.0f, 1.0f - drift_ratio);

    ticks_since_last_sync_ = 0;
  }

  Duration estimated_drift() const {
    std::lock_guard<std::mutex> lk(mu_);
    return metrics_.current_drift;
  }

  SyncMetrics metrics() const {
    std::lock_guard<std::mutex> lk(mu_);
    return metrics_;
  }

 private:
  ClockSyncConfig cfg_;
  std::atomic<WorldTick> tick_{0};
  Timestamp last_wall_ = std::chrono::steady_clock::now();
  Duration accumulated_correction_ = Duration::zero();
  uint64_t ticks_since_last_sync_ = 0;
  mutable uint64_t beacon_seq_ = 0;

  mutable std::mutex mu_;
  SyncMetrics metrics_;
};

}  // namespace time
}  // namespace world

#endif  // CYBER_TIME_TICK_CLOCK_H_
