/******************************************************************************
 * priority_tick_scheduler.h — Budget-aware priority scheduler.
 *
 * Each tick, the scheduler drains priority queues from P0 → P3.  When the
 * tick deadline is reached, remaining low-priority individuals are skipped.
 * P0_Critical individuals are *always* executed regardless of budget.
 *
 * PRD: #224 Priority-based unstable tick scheduling
 *****************************************************************************/
#ifndef CYBER_SCHEDULER_PRIORITY_TICK_SCHEDULER_H_
#define CYBER_SCHEDULER_PRIORITY_TICK_SCHEDULER_H_

#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "tick_priority.h"

namespace world {
namespace scheduler {

/// Opaque handle for an individual's tick handler.
using IndividualId = uint64_t;
using TickHandler  = std::function<void()>;

/// Registration record.
struct TickRegistration {
  IndividualId id;
  std::string  name;
  TickConfig   config;
  TickHandler  handler;
};

/// Budget-aware priority tick scheduler.
class PriorityTickScheduler {
 public:
  using Clock = std::chrono::steady_clock;

  explicit PriorityTickScheduler(
      std::chrono::microseconds tick_budget =
          std::chrono::microseconds(1'000'000 / 60))
      : tick_budget_(tick_budget) {}

  /// Register an individual's tick handler with a priority config.
  void Register(IndividualId id, const std::string& name,
                TickConfig config, TickHandler handler) {
    std::lock_guard<std::mutex> lk(mu_);
    TickRegistration reg{id, name, config, std::move(handler)};
    int bucket = static_cast<int>(config.priority);
    priority_queues_[bucket].push_back(reg);
    id_to_priority_[id] = config.priority;
  }

  /// Unregister an individual.
  void Unregister(IndividualId id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = id_to_priority_.find(id);
    if (it == id_to_priority_.end()) return;
    int bucket = static_cast<int>(it->second);
    auto& q = priority_queues_[bucket];
    q.erase(std::remove_if(q.begin(), q.end(),
                            [id](const TickRegistration& r) {
                              return r.id == id;
                            }),
            q.end());
    id_to_priority_.erase(it);
  }

  /// Execute one tick.  Returns per-priority statistics for this tick.
  std::array<PriorityStats, kNumPriorityLevels> RunOnce() {
    std::lock_guard<std::mutex> lk(mu_);
    auto deadline = Clock::now() + tick_budget_;

    std::array<PriorityStats, kNumPriorityLevels> stats{};

    for (int p = 0; p < kNumPriorityLevels; ++p) {
      for (auto& reg : priority_queues_[p]) {
        bool over_budget = Clock::now() >= deadline;

        // P0 (Critical) always runs regardless of budget.
        if (over_budget && reg.config.allow_skip &&
            reg.config.priority != TickPriority::kCritical) {
          stats[p].ticks_skipped++;
          continue;
        }

        auto t0 = Clock::now();
        reg.handler();
        auto elapsed_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - t0)
                .count());

        stats[p].ticks_executed++;
        stats[p].total_exec_us += elapsed_us;
        if (elapsed_us > stats[p].max_exec_us) {
          stats[p].max_exec_us = elapsed_us;
        }
      }
    }
    return stats;
  }

  /// Update tick budget (e.g. when target_tick_hz changes).
  void set_tick_budget(std::chrono::microseconds budget) {
    std::lock_guard<std::mutex> lk(mu_);
    tick_budget_ = budget;
  }

  /// Snapshot of all registrations (for diagnostics).
  std::vector<TickRegistration> registrations() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<TickRegistration> all;
    for (const auto& q : priority_queues_) {
      all.insert(all.end(), q.begin(), q.end());
    }
    return all;
  }

 private:
  mutable std::mutex mu_;
  std::chrono::microseconds tick_budget_;
  std::array<std::vector<TickRegistration>, kNumPriorityLevels>
      priority_queues_;
  std::unordered_map<IndividualId, TickPriority> id_to_priority_;
};

}  // namespace scheduler
}  // namespace world

#endif  // CYBER_SCHEDULER_PRIORITY_TICK_SCHEDULER_H_
