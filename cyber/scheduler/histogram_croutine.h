/******************************************************************************
 * histogram_croutine.h — Histogram CRoutine task for individual state arrays
 *
 * PRD #56: Uses `if constexpr` instead of `std::bool_constant` tag dispatch.
 * Inclusive and exclusive binning paths are mutually exclusive at compile time.
 *
 * Sources:
 *   - NVIDIA CUB: DeviceHistogram (multi-level histogram)
 *   - Apollo CyberRT: scheduler croutine pattern
 *   - pubsub-loop: world/scheduler/sort_histogram.py (reference semantics)
 *
 * Namespace: world::cyber::scheduler (migrated from apollo::cyber)
 *****************************************************************************/

#ifndef CYBER_SCHEDULER_HISTOGRAM_CROUTINE_H_
#define CYBER_SCHEDULER_HISTOGRAM_CROUTINE_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace world {
namespace cyber {
namespace scheduler {

/**
 * HistogramBuffer — fixed-bin histogram accumulator.
 *
 * Lightweight POD-ish type; no dynamic allocation.
 * Maximum 1024 bins (compile-time constant for stack allocation).
 */
class HistogramBuffer {
 public:
  static constexpr std::size_t kMaxBins = 1024;

  HistogramBuffer() = default;

  HistogramBuffer(std::size_t num_bins, float range_min, float range_max)
      : num_bins_(num_bins < kMaxBins ? num_bins : kMaxBins),
        range_min_(range_min),
        range_max_(range_max) {
    reset();
  }

  void reset() noexcept {
    std::memset(counts_, 0, sizeof(uint64_t) * num_bins_);
  }

  // Accessors
  std::size_t num_bins()  const noexcept { return num_bins_; }
  float       range_min() const noexcept { return range_min_; }
  float       range_max() const noexcept { return range_max_; }
  float       bin_width() const noexcept {
    return (range_max_ - range_min_) / static_cast<float>(num_bins_);
  }

  uint64_t  count(std::size_t bin) const noexcept { return counts_[bin]; }
  uint64_t* counts() noexcept { return counts_; }
  const uint64_t* counts() const noexcept { return counts_; }

  uint64_t total() const noexcept {
    uint64_t sum = 0;
    for (std::size_t i = 0; i < num_bins_; ++i) sum += counts_[i];
    return sum;
  }

 private:
  std::size_t num_bins_ = 16;
  float range_min_ = 0.0f;
  float range_max_ = 1.0f;
  uint64_t counts_[kMaxBins] = {};
};

/**
 * HistogramCRoutine — bin individual state values into a fixed histogram.
 *
 * @tparam StateT     Element type (must be convertible to float, or use
 *                    execute_with_key with a projection)
 * @tparam Inclusive  true  → value == bin_edge goes to LEFT bin  (floor)
 *                   false → value == bin_edge goes to RIGHT bin (ceil-1)
 *
 * if constexpr eliminates the unused binning path at template instantiation.
 *
 * Usage:
 *   HistogramBuffer hist(16, 0.0f, 1.0f);
 *   HistogramCRoutine<float, true> histo;
 *   histo.execute({data, n}, hist);
 */
template <typename StateT, bool Inclusive>
class HistogramCRoutine {
 public:
  struct Span {
    const StateT* data;
    std::size_t size;
  };

  HistogramCRoutine() = default;

  /**
   * Bin all elements into *hist*.
   *
   * Elements outside [range_min, range_max] are clamped to the first/last bin.
   * Empty input is a safe no-op.
   */
  void execute(Span input, HistogramBuffer& hist) noexcept {
    if (input.size == 0) return;

    const std::size_t num_bins = hist.num_bins();
    const float rmin = hist.range_min();
    const float rmax = hist.range_max();
    if (rmax <= rmin) return;

    const float inv_width = static_cast<float>(num_bins) / (rmax - rmin);
    uint64_t* counts = hist.counts();

    for (std::size_t i = 0; i < input.size; ++i) {
      float val = static_cast<float>(input.data[i]);
      bin_one(val, counts, num_bins, rmin, inv_width);
    }
  }

  /**
   * Bin with a custom projection.
   *
   * @tparam Proj  Callable: const StateT& → float
   */
  template <typename Proj>
  void execute_with_key(Span input, HistogramBuffer& hist, Proj proj) noexcept {
    if (input.size == 0) return;

    const std::size_t num_bins = hist.num_bins();
    const float rmin = hist.range_min();
    const float rmax = hist.range_max();
    if (rmax <= rmin) return;

    const float inv_width = static_cast<float>(num_bins) / (rmax - rmin);
    uint64_t* counts = hist.counts();

    for (std::size_t i = 0; i < input.size; ++i) {
      float val = proj(input.data[i]);
      bin_one(val, counts, num_bins, rmin, inv_width);
    }
  }

  static constexpr bool is_inclusive = Inclusive;

 private:
  /**
   * Bin a single value — the hot inner loop body.
   *
   * if constexpr selects inclusive vs exclusive at compile time.
   * The branch the compiler doesn't pick is never emitted.
   */
  static void bin_one(float val, uint64_t* counts,
                      std::size_t num_bins, float rmin,
                      float inv_width) noexcept {
    float raw = (val - rmin) * inv_width;

    if constexpr (Inclusive) {
      // Inclusive: value == edge → left bin (floor)
      auto idx = static_cast<std::ptrdiff_t>(raw);
      if (idx >= static_cast<std::ptrdiff_t>(num_bins)) {
        idx = static_cast<std::ptrdiff_t>(num_bins) - 1;
      }
      if (idx < 0) idx = 0;
      counts[static_cast<std::size_t>(idx)]++;
    } else {
      // Exclusive: value == edge → right bin
      auto idx = static_cast<std::ptrdiff_t>(raw);
      // If raw is exactly an integer and > 0, shift left
      if (raw == static_cast<float>(idx) && idx > 0) {
        idx--;
      }
      if (idx >= static_cast<std::ptrdiff_t>(num_bins)) {
        idx = static_cast<std::ptrdiff_t>(num_bins) - 1;
      }
      if (idx < 0) idx = 0;
      counts[static_cast<std::size_t>(idx)]++;
    }
  }
};

// Convenience aliases
template <typename StateT>
using InclusiveHistogramCRoutine = HistogramCRoutine<StateT, true>;

template <typename StateT>
using ExclusiveHistogramCRoutine = HistogramCRoutine<StateT, false>;

}  // namespace scheduler
}  // namespace cyber
}  // namespace world

#endif  // CYBER_SCHEDULER_HISTOGRAM_CROUTINE_H_
