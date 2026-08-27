/******************************************************************************
 * segment_map.h — Hierarchical segment map for batch individual operations
 *
 * PRD #107: Multi-segment hierarchical individual scanning
 *
 * Defines segment boundaries for hierarchical individual populations.
 * Each segment corresponds to one parent individual's sub-individual group.
 * Used by scheduler for segmented prefix scan, reduction, and dispatch.
 *
 * Sources:
 *   - CUB DeviceSegmentedReduce pattern
 *   - CyberRT scheduler batch dispatch
 *
 * Namespace: world::cyber::scheduler
 *****************************************************************************/

#ifndef CYBER_SCHEDULER_SEGMENT_MAP_H_
#define CYBER_SCHEDULER_SEGMENT_MAP_H_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

namespace world {
namespace cyber {
namespace scheduler {

/**
 * SegmentMap — offset-based segment boundary definition.
 *
 * Segments are defined by a sorted offset array: [0, 10, 20, 70]
 * means 3 segments spanning [0,10), [10,20), [20,70).
 *
 * Thread-safety: not thread-safe. Caller must synchronize mutations.
 */
class SegmentMap {
 public:
  /// Build from explicit offsets. offsets must be sorted and start with 0.
  /// Segment i spans indices [offsets[i], offsets[i+1]).
  explicit SegmentMap(std::vector<size_t> offsets)
      : offsets_(std::move(offsets)) {
    assert(offsets_.size() >= 1 && "offsets must have at least the start");
    assert(offsets_.front() == 0 && "offsets must start with 0");
    for (size_t i = 1; i < offsets_.size(); ++i) {
      assert(offsets_[i] >= offsets_[i - 1] && "offsets must be sorted");
    }
  }

  /// Build from per-segment sizes: [10, 10, 50] → offsets [0, 10, 20, 70]
  static SegmentMap FromSizes(const std::vector<size_t>& sizes) {
    std::vector<size_t> offsets;
    offsets.reserve(sizes.size() + 1);
    offsets.push_back(0);
    for (auto s : sizes) {
      offsets.push_back(offsets.back() + s);
    }
    return SegmentMap(std::move(offsets));
  }

  // --- Queries ---

  /// Number of segments
  size_t SegmentCount() const {
    return offsets_.size() > 0 ? offsets_.size() - 1 : 0;
  }

  /// Size of segment i
  size_t SegmentSize(size_t segment_idx) const {
    assert(segment_idx < SegmentCount());
    return offsets_[segment_idx + 1] - offsets_[segment_idx];
  }

  /// Start index of segment i
  size_t SegmentOffset(size_t segment_idx) const {
    assert(segment_idx < SegmentCount());
    return offsets_[segment_idx];
  }

  /// End index (exclusive) of segment i
  size_t SegmentEnd(size_t segment_idx) const {
    assert(segment_idx < SegmentCount());
    return offsets_[segment_idx + 1];
  }

  /// Total size across all segments
  size_t TotalSize() const { return offsets_.empty() ? 0 : offsets_.back(); }

  /// Locate a global index → (segment_idx, local_offset)
  /// Uses binary search: O(log N) where N = segment count.
  std::pair<size_t, size_t> Locate(size_t global_idx) const {
    assert(global_idx < TotalSize());
    // Find the last offset <= global_idx
    auto it = std::upper_bound(offsets_.begin(), offsets_.end(), global_idx);
    --it;
    size_t seg = static_cast<size_t>(it - offsets_.begin());
    return {seg, global_idx - *it};
  }

  /// Raw access to offset array
  const std::vector<size_t>& Offsets() const { return offsets_; }

  // --- Mutation ---

  /// Insert a new segment at segment_idx with given size.
  /// All subsequent segments shift right.
  void InsertSegment(size_t segment_idx, size_t size) {
    assert(segment_idx <= SegmentCount());
    size_t start = offsets_[segment_idx];
    // Insert new boundary
    offsets_.insert(offsets_.begin() + static_cast<ptrdiff_t>(segment_idx) + 1,
                    start + size);
    // Shift all subsequent offsets
    for (size_t i = segment_idx + 2; i < offsets_.size(); ++i) {
      offsets_[i] += size;
    }
  }

  /// Remove segment at segment_idx.
  void RemoveSegment(size_t segment_idx) {
    assert(segment_idx < SegmentCount());
    size_t removed_size = SegmentSize(segment_idx);
    offsets_.erase(offsets_.begin() + static_cast<ptrdiff_t>(segment_idx) + 1);
    // Shift subsequent offsets down
    for (size_t i = segment_idx + 1; i < offsets_.size(); ++i) {
      offsets_[i] -= removed_size;
    }
  }

  /// Resize segment at segment_idx.
  void ResizeSegment(size_t segment_idx, size_t new_size) {
    assert(segment_idx < SegmentCount());
    size_t old_size = SegmentSize(segment_idx);
    ptrdiff_t delta = static_cast<ptrdiff_t>(new_size) -
                      static_cast<ptrdiff_t>(old_size);
    for (size_t i = segment_idx + 1; i < offsets_.size(); ++i) {
      offsets_[i] = static_cast<size_t>(
          static_cast<ptrdiff_t>(offsets_[i]) + delta);
    }
  }

 private:
  std::vector<size_t> offsets_;
};

// ---------------------------------------------------------------------------
// Segmented batch operations (free functions)
// ---------------------------------------------------------------------------

/**
 * Segmented inclusive scan: applies op cumulatively within each segment,
 * resetting at segment boundaries.
 */
template <typename T, typename BinaryOp>
void SegmentedInclusiveScan(const SegmentMap& map,
                            std::span<const T> input,
                            std::span<T> output,
                            BinaryOp op) {
  assert(input.size() == map.TotalSize());
  assert(output.size() == map.TotalSize());

  for (size_t seg = 0; seg < map.SegmentCount(); ++seg) {
    size_t begin = map.SegmentOffset(seg);
    size_t end = map.SegmentEnd(seg);
    if (begin >= end) continue;

    output[begin] = input[begin];
    for (size_t i = begin + 1; i < end; ++i) {
      output[i] = op(output[i - 1], input[i]);
    }
  }
}

/**
 * Segmented exclusive scan: like inclusive but shifted right, starting
 * with identity at each segment boundary.
 */
template <typename T, typename BinaryOp>
void SegmentedExclusiveScan(const SegmentMap& map,
                            std::span<const T> input,
                            std::span<T> output,
                            BinaryOp op,
                            T identity) {
  assert(input.size() == map.TotalSize());
  assert(output.size() == map.TotalSize());

  for (size_t seg = 0; seg < map.SegmentCount(); ++seg) {
    size_t begin = map.SegmentOffset(seg);
    size_t end = map.SegmentEnd(seg);
    if (begin >= end) continue;

    T acc = identity;
    for (size_t i = begin; i < end; ++i) {
      output[i] = acc;
      acc = op(acc, input[i]);
    }
  }
}

/**
 * Segmented reduction: reduces each segment independently.
 * Returns one value per segment.
 */
template <typename T, typename BinaryOp>
std::vector<T> SegmentedReduce(const SegmentMap& map,
                               std::span<const T> input,
                               BinaryOp op,
                               T identity) {
  assert(input.size() == map.TotalSize());

  std::vector<T> result(map.SegmentCount());
  for (size_t seg = 0; seg < map.SegmentCount(); ++seg) {
    size_t begin = map.SegmentOffset(seg);
    size_t end = map.SegmentEnd(seg);
    T acc = identity;
    for (size_t i = begin; i < end; ++i) {
      acc = op(acc, input[i]);
    }
    result[seg] = acc;
  }
  return result;
}

/**
 * Segmented gather: extracts one element per segment at given local offset.
 * If local_offset >= segment size, uses default_val.
 */
template <typename T>
std::vector<T> SegmentedGather(const SegmentMap& map,
                               std::span<const T> input,
                               size_t local_offset,
                               T default_val = T{}) {
  std::vector<T> result(map.SegmentCount());
  for (size_t seg = 0; seg < map.SegmentCount(); ++seg) {
    if (local_offset < map.SegmentSize(seg)) {
      result[seg] = input[map.SegmentOffset(seg) + local_offset];
    } else {
      result[seg] = default_val;
    }
  }
  return result;
}

}  // namespace scheduler
}  // namespace cyber
}  // namespace world

#endif  // CYBER_SCHEDULER_SEGMENT_MAP_H_
