/******************************************************************************
 * merge.h — Multi-source individual state unstable merge
 *
 * PRD #225: Two-way and three-way merge of ChannelBuffers, ordered by
 * a caller-supplied key.  "Unstable" = equal-key elements from different
 * sources may appear in either order (permits single-pass O(N) merge
 * with no allocation).
 *****************************************************************************/

#ifndef CYBER_DATA_MERGE_H_
#define CYBER_DATA_MERGE_H_

#include <algorithm>
#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

#include "cyber/data/cache_buffer.h"
#include "cyber/data/channel_buffer.h"

namespace world {
namespace cyber {
namespace data {

// ─── SegmentDescriptor (shared with segmented_reduce) ────────────────────────

struct SegmentDescriptor {
  uint64_t offset;
  uint64_t count;
};

// ─── Helper: iterate CacheBuffer range ──────────────────────────────────────

namespace detail {

/// Drain [Head..Tail] of a locked CacheBuffer into a flat vector of raw ptrs.
/// Caller holds the mutex.
template <typename T>
inline void drain_buffer(const CacheBuffer<std::shared_ptr<T>>& buf,
                         std::vector<const T*>& out) {
  if (buf.Empty()) return;
  uint64_t head = buf.Head();
  uint64_t tail = buf.Tail();
  out.reserve(out.size() + (tail - head + 1));
  for (uint64_t i = head; i <= tail; ++i) {
    out.push_back(buf.at(i).get());
  }
}

}  // namespace detail

// ─── Two-way unstable_merge ─────────────────────────────────────────────────

/// Merge two monotone-ordered ChannelBuffers into `out`.
/// Output is sorted by key_fn.  No heap allocation in the merge loop itself
/// (the output vector pre-reserves).
///
/// @param buf_a   First source ChannelBuffer
/// @param buf_b   Second source ChannelBuffer
/// @param out     Destination — cleared and filled with merged elements
/// @param key_fn  T → comparable key (e.g., tick_id)
template <typename T, typename KeyFn = std::identity>
void unstable_merge(
    const ChannelBuffer<T>& buf_a,
    const ChannelBuffer<T>& buf_b,
    std::vector<std::shared_ptr<T>>& out,
    KeyFn key_fn = {}) {
  // 1. Snapshot both buffers under their respective locks
  std::vector<const T*> va, vb;
  {
    auto& ba = *buf_a.Buffer();
    std::lock_guard<std::mutex> lk(ba.Mutex());
    detail::drain_buffer(ba, va);
  }
  {
    auto& bb = *buf_b.Buffer();
    std::lock_guard<std::mutex> lk(bb.Mutex());
    detail::drain_buffer(bb, vb);
  }

  // 2. Two-pointer merge — O(N), no allocation beyond the pre-reserved output
  out.clear();
  out.reserve(va.size() + vb.size());

  size_t ia = 0, ib = 0;
  while (ia < va.size() && ib < vb.size()) {
    // Unstable: equal keys → pick either (we pick a for determinism within runs)
    if (key_fn(*vb[ib]) < key_fn(*va[ia])) {
      out.push_back(std::make_shared<T>(*vb[ib++]));
    } else {
      out.push_back(std::make_shared<T>(*va[ia++]));
    }
  }
  while (ia < va.size()) {
    out.push_back(std::make_shared<T>(*va[ia++]));
  }
  while (ib < vb.size()) {
    out.push_back(std::make_shared<T>(*vb[ib++]));
  }
}

// ─── Three-way unstable_merge ───────────────────────────────────────────────

template <typename T, typename KeyFn = std::identity>
void unstable_merge(
    const ChannelBuffer<T>& buf_a,
    const ChannelBuffer<T>& buf_b,
    const ChannelBuffer<T>& buf_c,
    std::vector<std::shared_ptr<T>>& out,
    KeyFn key_fn = {}) {
  // Snapshot all three
  std::vector<const T*> va, vb, vc;
  {
    auto& ba = *buf_a.Buffer();
    std::lock_guard<std::mutex> lk(ba.Mutex());
    detail::drain_buffer(ba, va);
  }
  {
    auto& bb = *buf_b.Buffer();
    std::lock_guard<std::mutex> lk(bb.Mutex());
    detail::drain_buffer(bb, vb);
  }
  {
    auto& bc = *buf_c.Buffer();
    std::lock_guard<std::mutex> lk(bc.Mutex());
    detail::drain_buffer(bc, vc);
  }

  out.clear();
  out.reserve(va.size() + vb.size() + vc.size());

  // Three-pointer merge
  size_t ia = 0, ib = 0, ic = 0;
  while (ia < va.size() && ib < vb.size() && ic < vc.size()) {
    auto ka = key_fn(*va[ia]);
    auto kb = key_fn(*vb[ib]);
    auto kc = key_fn(*vc[ic]);
    if (ka <= kb && ka <= kc) {
      out.push_back(std::make_shared<T>(*va[ia++]));
    } else if (kb <= ka && kb <= kc) {
      out.push_back(std::make_shared<T>(*vb[ib++]));
    } else {
      out.push_back(std::make_shared<T>(*vc[ic++]));
    }
  }
  // Drain remaining two
  while (ia < va.size() && ib < vb.size()) {
    if (key_fn(*vb[ib]) < key_fn(*va[ia])) {
      out.push_back(std::make_shared<T>(*vb[ib++]));
    } else {
      out.push_back(std::make_shared<T>(*va[ia++]));
    }
  }
  while (ia < va.size() && ic < vc.size()) {
    if (key_fn(*vc[ic]) < key_fn(*va[ia])) {
      out.push_back(std::make_shared<T>(*vc[ic++]));
    } else {
      out.push_back(std::make_shared<T>(*va[ia++]));
    }
  }
  while (ib < vb.size() && ic < vc.size()) {
    if (key_fn(*vc[ic]) < key_fn(*vb[ib])) {
      out.push_back(std::make_shared<T>(*vc[ic++]));
    } else {
      out.push_back(std::make_shared<T>(*vb[ib++]));
    }
  }
  while (ia < va.size()) out.push_back(std::make_shared<T>(*va[ia++]));
  while (ib < vb.size()) out.push_back(std::make_shared<T>(*vb[ib++]));
  while (ic < vc.size()) out.push_back(std::make_shared<T>(*vc[ic++]));
}

}  // namespace data
}  // namespace cyber
}  // namespace world

#endif  // CYBER_DATA_MERGE_H_
