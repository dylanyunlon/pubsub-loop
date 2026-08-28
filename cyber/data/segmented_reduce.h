/******************************************************************************
 * segmented_reduce.h — Per-segment scalar reduction over ChannelBuffer
 *
 * PRD #225: Single-pass segmented reduction.
 * Each SegmentDescriptor defines a contiguous slice [offset, offset+count).
 * result[i] = reduce(init, transform(buf[seg[i].offset]), ...,
 *                          transform(buf[seg[i].offset + seg[i].count - 1]))
 *****************************************************************************/

#ifndef CYBER_DATA_SEGMENTED_REDUCE_H_
#define CYBER_DATA_SEGMENTED_REDUCE_H_

#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <type_traits>

#include "cyber/data/cache_buffer.h"
#include "cyber/data/channel_buffer.h"
#include "cyber/data/merge.h"  // for SegmentDescriptor

namespace world {
namespace cyber {
namespace data {

/// Per-segment reduction over a ChannelBuffer.
///
/// @param buf        The source ChannelBuffer (all segments concatenated)
/// @param segments   Segment descriptors — offset is buffer-relative from Head()
/// @param results    Output span; must be pre-sized to segments.size()
/// @param init       Identity value for the reduction
/// @param reduce_op  Binary reduction: (U, U) -> U
/// @param transform  Unary transform: T -> U  (applied before reduce)
///
/// Single-pass: iterates the buffer once, dispatching each element to its
/// segment's accumulator.  O(N_total + P) where P = segments.size().
template <typename T, typename U,
          typename ReduceOp    = std::plus<U>,
          typename TransformOp = std::identity>
void segmented_reduce(
    const ChannelBuffer<T>&              buf,
    std::span<const SegmentDescriptor>   segments,
    std::span<U>                         results,
    U                                    init,
    ReduceOp                             reduce_op   = {},
    TransformOp                          transform   = {}) {
  // Initialize all segment results to identity
  for (size_t i = 0; i < segments.size(); ++i) {
    results[i] = init;
  }

  auto& cache = *buf.Buffer();
  std::lock_guard<std::mutex> lk(cache.Mutex());

  if (cache.Empty()) return;

  uint64_t buf_head = cache.Head();

  // Single-pass: for each segment, reduce its slice
  for (size_t seg_idx = 0; seg_idx < segments.size(); ++seg_idx) {
    const auto& seg = segments[seg_idx];
    uint64_t start = buf_head + seg.offset;
    uint64_t end   = start + seg.count;

    // Clamp to buffer bounds
    uint64_t buf_tail = cache.Tail();
    if (start > buf_tail) continue;
    if (end > buf_tail + 1) end = buf_tail + 1;

    U acc = init;
    for (uint64_t i = start; i < end; ++i) {
      acc = reduce_op(acc, transform(*cache.at(i)));
    }
    results[seg_idx] = acc;
  }
}

/// Convenience: segmented reduce over a flat vector (post-merge output)
template <typename T, typename U,
          typename ReduceOp    = std::plus<U>,
          typename TransformOp = std::identity>
void segmented_reduce(
    const std::vector<std::shared_ptr<T>>& data,
    std::span<const SegmentDescriptor>     segments,
    std::span<U>                           results,
    U                                      init,
    ReduceOp                               reduce_op   = {},
    TransformOp                            transform   = {}) {
  for (size_t i = 0; i < segments.size(); ++i) {
    results[i] = init;
  }

  for (size_t seg_idx = 0; seg_idx < segments.size(); ++seg_idx) {
    const auto& seg = segments[seg_idx];
    if (seg.offset >= data.size()) continue;
    uint64_t end = std::min(seg.offset + seg.count,
                            static_cast<uint64_t>(data.size()));
    U acc = init;
    for (uint64_t i = seg.offset; i < end; ++i) {
      acc = reduce_op(acc, transform(*data[i]));
    }
    results[seg_idx] = acc;
  }
}

}  // namespace data
}  // namespace cyber
}  // namespace world

#endif  // CYBER_DATA_SEGMENTED_REDUCE_H_
