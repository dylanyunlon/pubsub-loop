/******************************************************************************
 * world_snapshot_analyzer.h — Bulk snapshot ingestion & divergence detection
 *
 * PRD #13: WorldSnapshotAnalyzer — the main entry point for cross-individual
 *          state divergence analysis, convergence tracking, and reporting.
 *
 * Namespace: world::cyber::statistics
 *****************************************************************************/

#ifndef CYBER_STATISTICS_WORLD_SNAPSHOT_ANALYZER_H_
#define CYBER_STATISTICS_WORLD_SNAPSHOT_ANALYZER_H_

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "cyber/statistics/convergence_tracker.h"
#include "cyber/statistics/divergence_metrics.h"
#include "cyber/statistics/world_snapshot_types.h"

namespace world {
namespace cyber {
namespace statistics {

enum class ReportFormat { Markdown, Json };

class WorldSnapshotAnalyzer {
 public:
  struct Config {
    double divergence_threshold = 0.1;
    size_t sliding_window_ticks = 100;
    DivergenceMetric metric = DivergenceMetric::L2Norm;
    bool track_convergence = true;
    size_t max_convergence_ticks = 500;
  };

  explicit WorldSnapshotAnalyzer(Config config = {});

  /// Ingest a single snapshot into the sliding window.
  void Ingest(const WorldSnapshot& snapshot);

  /// Ingest a batch of snapshots (bulk processing path).
  void IngestBatch(std::span<const WorldSnapshot> snapshots);

  /// Detect all currently-diverging individuals.
  std::vector<DivergenceReport> DetectDivergence() const;

  /// Analyze divergence status of a specific individual.
  std::optional<DivergenceReport> AnalyzeIndividual(
      const IndividualId& id) const;

  /// Compute group-level statistics from the latest snapshot.
  metrics::GroupStats ComputeGroupStats() const;

  /// Export convergence report to file.
  void ExportConvergenceReport(
      const std::filesystem::path& path,
      ReportFormat fmt = ReportFormat::Markdown) const;

  /// Reset analyzer state and discard all ingested data.
  void Reset();

  size_t IngestedSnapshotCount() const { return ingested_count_; }

 private:
  Config config_;
  std::vector<WorldSnapshot> snapshot_window_;
  size_t window_head_ = 0;
  size_t ingested_count_ = 0;
  std::unordered_map<IndividualId, DivergenceTracker> trackers_;

  /// Update all trackers from a single snapshot.
  void ProcessSnapshot(const WorldSnapshot& snapshot);
};

}  // namespace statistics
}  // namespace cyber
}  // namespace world

#endif  // CYBER_STATISTICS_WORLD_SNAPSHOT_ANALYZER_H_
