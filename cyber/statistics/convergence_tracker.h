/******************************************************************************
 * convergence_tracker.h — Divergence state-machine FSM per individual
 *
 * PRD #13: Normal → Diverging → DivergingChronic → Converging → Converged
 *
 * Namespace: world::cyber::statistics
 *****************************************************************************/

#ifndef CYBER_STATISTICS_CONVERGENCE_TRACKER_H_
#define CYBER_STATISTICS_CONVERGENCE_TRACKER_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "cyber/statistics/world_snapshot_types.h"

namespace world {
namespace cyber {
namespace statistics {

class DivergenceTracker {
 public:
  enum class Phase {
    Normal,
    Diverging,
    DivergingChronic,
    Converging,
    Converged
  };

  struct Config {
    double threshold = 0.1;
    size_t hysteresis_ticks = 5;        // consecutive below-threshold to confirm
    size_t chronic_threshold = 10;      // consecutive above-threshold → Chronic
    size_t max_convergence_ticks = 500;
  };

  explicit DivergenceTracker(IndividualId id, Config config = {});

  /// Feed one divergence score at a given tick; FSM transitions internally.
  void Update(TickId tick, double score);

  Phase CurrentPhase() const { return phase_; }
  double PeakDivergence() const { return peak_divergence_; }
  TickId DivergenceStartTick() const { return divergence_start_tick_; }
  TickId ConvergenceTick() const { return convergence_tick_; }
  size_t TicksToConverge() const;
  const IndividualId& Id() const { return id_; }

  DivergenceReport ToReport(TickId analysis_tick) const;

 private:
  IndividualId id_;
  Config config_;
  Phase phase_ = Phase::Normal;

  TickId divergence_start_tick_ = 0;
  TickId convergence_tick_ = 0;
  double peak_divergence_ = 0.0;
  double latest_score_ = 0.0;

  size_t consecutive_above_ = 0;
  size_t consecutive_below_ = 0;
};

}  // namespace statistics
}  // namespace cyber
}  // namespace world

#endif  // CYBER_STATISTICS_CONVERGENCE_TRACKER_H_
