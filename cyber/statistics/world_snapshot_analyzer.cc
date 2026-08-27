/******************************************************************************
 * world_snapshot_analyzer.cc — Bulk snapshot ingestion & divergence detection
 *
 * PRD #13: WorldSnapshotAnalyzer implementation.
 *****************************************************************************/

#include "cyber/statistics/world_snapshot_analyzer.h"

#include <fstream>
#include <sstream>

namespace world {
namespace cyber {
namespace statistics {

WorldSnapshotAnalyzer::WorldSnapshotAnalyzer(Config config)
    : config_(config) {
  snapshot_window_.resize(config_.sliding_window_ticks);
}

void WorldSnapshotAnalyzer::Ingest(const WorldSnapshot& snapshot) {
  // Ring-buffer insertion
  snapshot_window_[window_head_] = snapshot;
  window_head_ = (window_head_ + 1) % snapshot_window_.size();
  ingested_count_++;

  ProcessSnapshot(snapshot);
}

void WorldSnapshotAnalyzer::IngestBatch(
    std::span<const WorldSnapshot> snapshots) {
  for (const auto& snap : snapshots) {
    Ingest(snap);
  }
}

void WorldSnapshotAnalyzer::ProcessSnapshot(const WorldSnapshot& snapshot) {
  if (snapshot.individual_states.empty()) return;

  auto group = metrics::compute_group_stats(snapshot.individual_states);

  for (const auto& ind : snapshot.individual_states) {
    double score = 0.0;
    switch (config_.metric) {
      case DivergenceMetric::L2Norm:
        score = metrics::l2_divergence(ind.position, group.centroid);
        break;
      case DivergenceMetric::KLDivergence:
        // KL requires distribution form; fall back to L2 for position
        score = metrics::l2_divergence(ind.position, group.centroid);
        break;
      case DivergenceMetric::Wasserstein:
        // Wasserstein requires 1D distributions; fall back to L2 for position
        score = metrics::l2_divergence(ind.position, group.centroid);
        break;
    }

    if (config_.track_convergence) {
      auto it = trackers_.find(ind.id);
      if (it == trackers_.end()) {
        DivergenceTracker::Config tc;
        tc.threshold = config_.divergence_threshold;
        tc.max_convergence_ticks = config_.max_convergence_ticks;
        auto [inserted, _] = trackers_.emplace(
            ind.id, DivergenceTracker(ind.id, tc));
        it = inserted;
      }
      it->second.Update(snapshot.tick_id, score);
    }
  }
}

std::vector<DivergenceReport> WorldSnapshotAnalyzer::DetectDivergence() const {
  std::vector<DivergenceReport> reports;
  TickId latest_tick = 0;
  if (ingested_count_ > 0) {
    size_t last_idx =
        (window_head_ + snapshot_window_.size() - 1) % snapshot_window_.size();
    latest_tick = snapshot_window_[last_idx].tick_id;
  }

  for (const auto& [id, tracker] : trackers_) {
    auto phase = tracker.CurrentPhase();
    if (phase == DivergenceTracker::Phase::Diverging ||
        phase == DivergenceTracker::Phase::DivergingChronic ||
        phase == DivergenceTracker::Phase::Converging) {
      auto report = tracker.ToReport(latest_tick);
      report.metric_used = config_.metric;
      reports.push_back(std::move(report));
    }
  }
  return reports;
}

std::optional<DivergenceReport> WorldSnapshotAnalyzer::AnalyzeIndividual(
    const IndividualId& id) const {
  auto it = trackers_.find(id);
  if (it == trackers_.end()) return std::nullopt;

  TickId latest_tick = 0;
  if (ingested_count_ > 0) {
    size_t last_idx =
        (window_head_ + snapshot_window_.size() - 1) % snapshot_window_.size();
    latest_tick = snapshot_window_[last_idx].tick_id;
  }

  auto report = it->second.ToReport(latest_tick);
  report.metric_used = config_.metric;
  return report;
}

metrics::GroupStats WorldSnapshotAnalyzer::ComputeGroupStats() const {
  if (ingested_count_ == 0) return {};

  // Use the latest snapshot in the ring buffer
  size_t last_idx =
      (window_head_ + snapshot_window_.size() - 1) % snapshot_window_.size();
  return metrics::compute_group_stats(
      snapshot_window_[last_idx].individual_states);
}

void WorldSnapshotAnalyzer::ExportConvergenceReport(
    const std::filesystem::path& path, ReportFormat fmt) const {
  std::ofstream out(path);
  if (!out.is_open()) return;

  if (fmt == ReportFormat::Markdown) {
    out << "# Convergence Report\n\n";
    out << "| Individual | Phase | Divergence Score | Start Tick | "
           "Convergence Tick | Ticks to Converge | Peak |\n";
    out << "|---|---|---|---|---|---|---|\n";

    for (const auto& [id, tracker] : trackers_) {
      auto phase = tracker.CurrentPhase();
      const char* phase_str = "Normal";
      switch (phase) {
        case DivergenceTracker::Phase::Normal:
          phase_str = "Normal";
          break;
        case DivergenceTracker::Phase::Diverging:
          phase_str = "Diverging";
          break;
        case DivergenceTracker::Phase::DivergingChronic:
          phase_str = "Chronic";
          break;
        case DivergenceTracker::Phase::Converging:
          phase_str = "Converging";
          break;
        case DivergenceTracker::Phase::Converged:
          phase_str = "Converged";
          break;
      }

      TickId latest_tick = 0;
      if (ingested_count_ > 0) {
        size_t last_idx =
            (window_head_ + snapshot_window_.size() - 1) %
            snapshot_window_.size();
        latest_tick = snapshot_window_[last_idx].tick_id;
      }
      auto report = tracker.ToReport(latest_tick);

      out << "| " << id << " | " << phase_str << " | "
          << report.divergence_score << " | " << report.divergence_start_tick
          << " | "
          << (report.converged ? std::to_string(report.convergence_tick)
                               : "N/A")
          << " | "
          << (report.converged ? std::to_string(report.ticks_to_converge)
                               : "N/A")
          << " | " << report.peak_divergence << " |\n";
    }
  } else {
    // JSON format
    out << "{\n  \"reports\": [\n";
    bool first = true;
    for (const auto& [id, tracker] : trackers_) {
      TickId latest_tick = 0;
      if (ingested_count_ > 0) {
        size_t last_idx =
            (window_head_ + snapshot_window_.size() - 1) %
            snapshot_window_.size();
        latest_tick = snapshot_window_[last_idx].tick_id;
      }
      auto report = tracker.ToReport(latest_tick);

      if (!first) out << ",\n";
      first = false;
      out << "    {\n"
          << "      \"individual\": \"" << id << "\",\n"
          << "      \"divergence_score\": " << report.divergence_score << ",\n"
          << "      \"divergence_start_tick\": " << report.divergence_start_tick
          << ",\n"
          << "      \"converged\": " << (report.converged ? "true" : "false")
          << ",\n"
          << "      \"convergence_tick\": " << report.convergence_tick << ",\n"
          << "      \"ticks_to_converge\": " << report.ticks_to_converge
          << ",\n"
          << "      \"peak_divergence\": " << report.peak_divergence << "\n"
          << "    }";
    }
    out << "\n  ]\n}\n";
  }
}

void WorldSnapshotAnalyzer::Reset() {
  snapshot_window_.clear();
  snapshot_window_.resize(config_.sliding_window_ticks);
  window_head_ = 0;
  ingested_count_ = 0;
  trackers_.clear();
}

}  // namespace statistics
}  // namespace cyber
}  // namespace world
