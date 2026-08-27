/******************************************************************************
 * convergence_tracker.cc — Divergence FSM per-individual implementation
 *
 * PRD #13: Normal → Diverging → DivergingChronic → Converging → Converged
 *****************************************************************************/

#include "cyber/statistics/convergence_tracker.h"

#include <algorithm>

namespace world {
namespace cyber {
namespace statistics {

DivergenceTracker::DivergenceTracker(IndividualId id, Config config)
    : id_(std::move(id)), config_(config) {}

void DivergenceTracker::Update(TickId tick, double score) {
  latest_score_ = score;
  peak_divergence_ = std::max(peak_divergence_, score);

  bool above = score >= config_.threshold;

  if (above) {
    consecutive_above_++;
    consecutive_below_ = 0;
  } else {
    consecutive_below_++;
    consecutive_above_ = 0;
  }

  switch (phase_) {
    case Phase::Normal:
      if (above) {
        phase_ = Phase::Diverging;
        divergence_start_tick_ = tick;
        peak_divergence_ = score;
      }
      break;

    case Phase::Diverging:
      if (!above && consecutive_below_ >= config_.hysteresis_ticks) {
        phase_ = Phase::Converged;
        convergence_tick_ = tick;
      } else if (consecutive_above_ >= config_.chronic_threshold) {
        phase_ = Phase::DivergingChronic;
      }
      break;

    case Phase::DivergingChronic:
      if (!above) {
        phase_ = Phase::Converging;
      }
      break;

    case Phase::Converging:
      if (above) {
        // Relapse — back to Chronic
        phase_ = Phase::DivergingChronic;
      } else if (consecutive_below_ >= config_.hysteresis_ticks) {
        phase_ = Phase::Converged;
        convergence_tick_ = tick;
      }
      break;

    case Phase::Converged:
      // Terminal state within one divergence episode.
      // A new above-threshold reading starts a fresh episode.
      if (above) {
        phase_ = Phase::Diverging;
        divergence_start_tick_ = tick;
        peak_divergence_ = score;
        convergence_tick_ = 0;
      }
      break;
  }
}

size_t DivergenceTracker::TicksToConverge() const {
  if (convergence_tick_ == 0 || divergence_start_tick_ == 0) return 0;
  if (convergence_tick_ <= divergence_start_tick_) return 0;
  return static_cast<size_t>(convergence_tick_ - divergence_start_tick_);
}

DivergenceReport DivergenceTracker::ToReport(TickId analysis_tick) const {
  DivergenceReport report;
  report.outlier_id = id_;
  report.divergence_score = latest_score_;
  report.metric_used = DivergenceMetric::L2Norm;  // default; caller overrides
  report.divergence_start_tick = divergence_start_tick_;
  report.analysis_tick = analysis_tick;
  report.converged = (phase_ == Phase::Converged);
  report.convergence_tick = convergence_tick_;
  report.ticks_to_converge = TicksToConverge();
  report.peak_divergence = peak_divergence_;
  return report;
}

}  // namespace statistics
}  // namespace cyber
}  // namespace world
