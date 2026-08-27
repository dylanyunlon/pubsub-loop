/******************************************************************************
 * divergence_metrics.cc — L2 / KL / Wasserstein divergence implementations
 *
 * PRD #13: Cross-individual divergence detection metrics.
 *****************************************************************************/

#include "cyber/statistics/divergence_metrics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace world {
namespace cyber {
namespace statistics {
namespace metrics {

double kl_divergence(std::span<const double> p, std::span<const double> q,
                     double epsilon) {
  if (p.size() != q.size() || p.empty()) return 0.0;

  double kl = 0.0;
  for (size_t i = 0; i < p.size(); ++i) {
    double pi = p[i] + epsilon;
    double qi = q[i] + epsilon;
    kl += pi * std::log(pi / qi);
  }
  return kl;
}

double wasserstein_1d(std::span<const double> dist_a,
                      std::span<const double> dist_b) {
  if (dist_a.empty() || dist_b.empty()) return 0.0;

  // Sort copies of both distributions
  std::vector<double> a(dist_a.begin(), dist_a.end());
  std::vector<double> b(dist_b.begin(), dist_b.end());
  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());

  // Compute quantile-based EMD:
  // Resample both to max(len_a, len_b) uniform quantiles
  size_t n = std::max(a.size(), b.size());
  double emd = 0.0;
  for (size_t i = 0; i < n; ++i) {
    double t = static_cast<double>(i) / static_cast<double>(n - 1);
    // Linear interpolation into sorted arrays
    double idx_a = t * static_cast<double>(a.size() - 1);
    size_t lo_a = static_cast<size_t>(idx_a);
    size_t hi_a = std::min(lo_a + 1, a.size() - 1);
    double frac_a = idx_a - static_cast<double>(lo_a);
    double val_a = a[lo_a] * (1.0 - frac_a) + a[hi_a] * frac_a;

    double idx_b = t * static_cast<double>(b.size() - 1);
    size_t lo_b = static_cast<size_t>(idx_b);
    size_t hi_b = std::min(lo_b + 1, b.size() - 1);
    double frac_b = idx_b - static_cast<double>(lo_b);
    double val_b = b[lo_b] * (1.0 - frac_b) + b[hi_b] * frac_b;

    emd += std::abs(val_a - val_b);
  }
  return emd / static_cast<double>(n);
}

GroupStats compute_group_stats(const std::vector<IndividualState>& states) {
  GroupStats gs{};
  gs.individual_count = states.size();
  if (states.empty()) return gs;

  // Centroid and velocity mean
  for (const auto& s : states) {
    gs.centroid = gs.centroid + s.position;
    gs.velocity_mean = gs.velocity_mean + s.velocity;
  }
  double n = static_cast<double>(states.size());
  gs.centroid = gs.centroid / n;
  gs.velocity_mean = gs.velocity_mean / n;

  // Spread (max distance from centroid) and most divergent
  gs.spread = 0.0;
  gs.max_individual_divergence = 0.0;
  for (const auto& s : states) {
    double d = l2_divergence(s.position, gs.centroid);
    if (d > gs.spread) gs.spread = d;
    if (d > gs.max_individual_divergence) {
      gs.max_individual_divergence = d;
      gs.most_divergent = s.id;
    }
  }
  return gs;
}

double composite_divergence(const IndividualState& individual,
                            const GroupStats& group,
                            const DivergenceWeights& weights) {
  double pos_div = l2_divergence(individual.position, group.centroid);
  double vel_div = l2_divergence(individual.velocity, group.velocity_mean);

  // Attribute divergence: mean absolute deviation of all attributes
  // (compared to zero — group attribute mean would require a richer GroupStats)
  double attr_div = 0.0;
  if (!individual.attributes.empty()) {
    double sum = 0.0;
    for (const auto& [_, val] : individual.attributes) {
      sum += std::abs(val);
    }
    attr_div = sum / static_cast<double>(individual.attributes.size());
  }

  return weights.position_weight * pos_div +
         weights.velocity_weight * vel_div +
         weights.attribute_weight * attr_div;
}

}  // namespace metrics
}  // namespace statistics
}  // namespace cyber
}  // namespace world
