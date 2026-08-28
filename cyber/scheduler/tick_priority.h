/******************************************************************************
 * tick_priority.h — Priority-based unstable tick scheduling for individuals.
 *
 * High-priority individuals (foreground render, physics) execute first each
 * tick.  Low-priority individuals may skip frames when the tick budget is
 * exhausted.  "Unstable" = relative order among same-priority individuals
 * is not guaranteed between ticks.
 *
 * PRD: #224 Priority-based unstable tick scheduling
 *****************************************************************************/
#ifndef CYBER_SCHEDULER_TICK_PRIORITY_H_
#define CYBER_SCHEDULER_TICK_PRIORITY_H_

#include <cstdint>

namespace world {
namespace scheduler {

/// Individual tick priorities.  Lower numeric value = higher priority.
enum class TickPriority : uint8_t {
  kCritical   = 0,  // P0: never skips, strict deadline
  kHigh       = 1,  // P1: occasional skip tolerable (< 5%)
  kNormal     = 2,  // P2: default, best-effort every tick
  kBackground = 3,  // P3: skip freely under load
};

inline constexpr int kNumPriorityLevels = 4;

/// Per-individual tick configuration.
struct TickConfig {
  TickPriority priority    = TickPriority::kNormal;
  uint32_t     target_hz   = 30;    // desired tick frequency
  uint32_t     budget_us   = 0;     // per-execution budget (0 = unlimited)
  bool         allow_skip  = true;  // may the scheduler skip this individual?
};

/// Runtime statistics for one priority level.
struct PriorityStats {
  uint64_t ticks_executed  = 0;
  uint64_t ticks_skipped   = 0;
  uint64_t total_exec_us   = 0;
  uint64_t max_exec_us     = 0;

  double skip_rate() const {
    uint64_t total = ticks_executed + ticks_skipped;
    return total > 0 ? static_cast<double>(ticks_skipped) / total : 0.0;
  }
};

}  // namespace scheduler
}  // namespace world

#endif  // CYBER_SCHEDULER_TICK_PRIORITY_H_
