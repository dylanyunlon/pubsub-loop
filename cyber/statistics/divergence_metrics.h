/******************************************************************************
 * divergence_metrics.h — L2 / KL / Wasserstein divergence computations
 *
 * PRD #13: Cross-individual divergence detection metrics.
 *
 * Namespace: world::cyber::statistics::metrics
 *****************************************************************************/

#ifndef CYBER_STATISTICS_DIVERGENCE_METRICS_H_
#define CYBER_STATISTICS_DIVERGENCE_METRICS_H_

#include <algorithm>
#include <cmath>
#include <numeric>
#include <span>
#include <vector>

#include "cyber/statistics/world_snapshot_types.h"

namespace world {
namespace cyber {
namespace statistics {
namespace metrics {

// L2 Euclidean norm divergence between individual position and group centroid
inline double l2_divergence(const Vec3d& individual_pos,
                            const Vec3d& group_centroid) {
  auto diff = individual_pos - group_centroid;
  return std::sqrt(diff.norm_sq());
}

// KL divergence (discrete distribution, epsilon smoothing to avoid log(0))
double kl_divergence(std::span<const double> p, std::span<const double> q,
                     double epsilon = 1e-10);

// 1D Wasserstein distance (Earth Mover's Distance)
double wasserstein_1d(std::span<const double> dist_a,
                      std::span<const double> dist_b);

struct DivergenceWeights {
  double position_weight = 0.5;
  double velocity_weight = 0.3;
  double attribute_weight = 0.2;
};

struct GroupStats {
  Vec3d centroid;
  Vec3d velocity_mean;
  double spread;
  size_t individual_count;
  double max_individual_divergence;
  IndividualId most_divergent;
};

// Composite divergence: weighted position + velocity + attribute divergence
double composite_divergence(const IndividualState& individual,
                            const GroupStats& group,
                            const DivergenceWeights& weights = {});

// Compute group statistics (centroid, spread)
GroupStats compute_group_stats(const std::vector<IndividualState>& states);

}  // namespace metrics
}  // namespace statistics
}  // namespace cyber
}  // namespace world

#endif  // CYBER_STATISTICS_DIVERGENCE_METRICS_H_
