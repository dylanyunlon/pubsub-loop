/******************************************************************************
 * world_snapshot_types.h — World snapshot and divergence report types
 *
 * PRD #13: Bulk-process world state snapshots in statistics module for
 *          cross-individual state divergence detection and convergence
 *          reporting.
 *
 * Namespace: world::cyber::statistics
 *****************************************************************************/

#ifndef CYBER_STATISTICS_WORLD_SNAPSHOT_TYPES_H_
#define CYBER_STATISTICS_WORLD_SNAPSHOT_TYPES_H_

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace world {
namespace cyber {
namespace statistics {

using TickId = uint64_t;
using Timestamp = uint64_t;
using IndividualId = std::string;

struct Vec3d {
  double x = 0.0, y = 0.0, z = 0.0;
  Vec3d operator-(const Vec3d& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3d operator+(const Vec3d& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3d operator/(double s) const { return {x / s, y / s, z / s}; }
  double norm_sq() const { return x * x + y * y + z * z; }
  double norm() const { return std::sqrt(norm_sq()); }
};

struct Quaterniond {
  double w = 1.0, x = 0.0, y = 0.0, z = 0.0;
};

struct IndividualState {
  IndividualId id;
  Vec3d position;
  Vec3d velocity;
  Quaterniond orientation;
  std::unordered_map<std::string, double> attributes;
};

struct WorldSnapshot {
  TickId tick_id;
  Timestamp timestamp;
  std::vector<IndividualState> individual_states;
};

enum class DivergenceMetric { L2Norm, KLDivergence, Wasserstein };

struct DivergenceReport {
  IndividualId outlier_id;
  double divergence_score;
  DivergenceMetric metric_used;
  TickId divergence_start_tick;
  TickId analysis_tick;
  bool converged;
  TickId convergence_tick;
  size_t ticks_to_converge;
  double peak_divergence;
};

}  // namespace statistics
}  // namespace cyber
}  // namespace world

#endif  // CYBER_STATISTICS_WORLD_SNAPSHOT_TYPES_H_
