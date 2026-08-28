/******************************************************************************
 * warp_reduce.h — Butterfly warp-level reduction for scheduler load balancing
 *
 * PRD #235: Replace sequential warp_shfl_down chain (31 serial ops) with
 * butterfly __shfl_xor pattern (5 independent rounds for warp=32).
 * 6× fewer serial stalls in the load-balance aggregation kernel.
 *
 * For non-CUDA builds, provides a CPU thread-local fallback.
 *****************************************************************************/

#ifndef CYBER_SCHEDULER_WARP_REDUCE_H_
#define CYBER_SCHEDULER_WARP_REDUCE_H_

#include <cstdint>
#include <type_traits>

#if defined(__CUDACC__)
  #define WR_DEVICE __device__
  #define WR_HOST_DEVICE __host__ __device__
  #define WR_HAS_CUDA 1
#else
  #define WR_DEVICE
  #define WR_HOST_DEVICE
  #define WR_HAS_CUDA 0
#endif

namespace world {
namespace scheduler {

#if WR_HAS_CUDA

// ─── Generic butterfly warp reduction ───────────────────────────────────────

/// Butterfly reduction across a warp using __shfl_xor_sync.
/// 5 rounds for warp size 32 (log2(32) = 5).
/// T must fit in a single 32-bit or 64-bit shuffle register.
template <typename T, typename BinaryOp>
WR_DEVICE T warp_reduce(T val, BinaryOp op,
                         unsigned mask = 0xFFFFFFFF) {
  static_assert(sizeof(T) <= 8,
      "warp_reduce requires T to fit in a single shuffle register");

  #pragma unroll
  for (int delta = warpSize / 2; delta > 0; delta >>= 1) {
    T peer = __shfl_xor_sync(mask, val, delta);
    val = op(val, peer);
  }
  return val;
}

// ─── Convenience specializations ────────────────────────────────────────────

struct Plus {
  template <typename T>
  WR_DEVICE T operator()(T a, T b) const { return a + b; }
};

struct Max {
  template <typename T>
  WR_DEVICE T operator()(T a, T b) const { return a > b ? a : b; }
};

struct Min {
  template <typename T>
  WR_DEVICE T operator()(T a, T b) const { return a < b ? a : b; }
};

WR_DEVICE inline float warp_reduce_sum(float v,
                                         unsigned mask = 0xFFFFFFFF) {
  return warp_reduce(v, Plus{}, mask);
}

WR_DEVICE inline float warp_reduce_max(float v,
                                         unsigned mask = 0xFFFFFFFF) {
  return warp_reduce(v, Max{}, mask);
}

WR_DEVICE inline float warp_reduce_min(float v,
                                         unsigned mask = 0xFFFFFFFF) {
  return warp_reduce(v, Min{}, mask);
}

WR_DEVICE inline int warp_reduce_add(int v,
                                       unsigned mask = 0xFFFFFFFF) {
  return warp_reduce(v, Plus{}, mask);
}

// ─── Partial warp support ───────────────────────────────────────────────────

/// Reduce over only `active_lanes` lanes (for the last warp in a block
/// where not all lanes have valid data).
WR_DEVICE inline float warp_reduce_sum_partial(float v,
                                                 int active_lanes) {
  unsigned mask = (active_lanes >= 32)
                      ? 0xFFFFFFFF
                      : ((1u << active_lanes) - 1u);
  return warp_reduce(v, Plus{}, mask);
}

#endif  // WR_HAS_CUDA

// ─── CPU fallback (for unit tests without GPU) ──────────────────────────────

/// Single-threaded "warp reduce" — just returns the input.
/// Used in CPU-only builds for API compatibility.
template <typename T, typename BinaryOp>
inline T warp_reduce_cpu(T val, BinaryOp /*op*/) {
  return val;  // No actual reduction needed on CPU (single "lane")
}

}  // namespace scheduler
}  // namespace world

#undef WR_DEVICE
#undef WR_HOST_DEVICE

#endif  // CYBER_SCHEDULER_WARP_REDUCE_H_
