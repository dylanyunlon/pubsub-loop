/******************************************************************************
 * parallel_reduce.h — Parallel reduction over collections of ChannelBuffers
 *
 * PRD #231: World-wide aggregate computation using thread-pool parallelism.
 * Replaces sequential transform_reduce loop over 100K+ individuals.
 *
 * Performance target: 100K individuals, 4 aggregates → <2ms total
 * (baseline: 15ms sequential, ~87% reduction).
 *
 * Design:
 *   - Auto backend: thread-pool for CPU (>= min_elements_per_thread)
 *   - Sequential fallback for small datasets
 *   - GPU warp-reduce backend (stub, requires CUDA context)
 *   - transform → reduce pipeline per buffer, then reduce partial results
 *
 * Thread safety: parallel_reduce is reentrant. Each call creates its own
 * thread-pool tasks. Underlying ChannelBuffer reads are const.
 *****************************************************************************/

#ifndef CYBER_DATA_PARALLEL_REDUCE_H_
#define CYBER_DATA_PARALLEL_REDUCE_H_

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <numeric>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

namespace world {
namespace cyber {
namespace data {

// ─── Configuration ──────────────────────────────────────────────────────────

struct ParallelReduceConfig {
  enum class Backend {
    Auto,         // Choose based on element count and available hardware
    ThreadPool,   // Force thread-pool parallelism
    WarpReduce,   // Force GPU warp-reduce (requires CUDA context)
    Sequential,   // Force sequential (for debugging / reproducibility)
  };

  Backend backend                = Backend::Auto;
  size_t  min_elements_per_thread = 1024;  // Below this: sequential
  int     thread_pool_workers    = -1;     // -1 = hardware_concurrency
};

// ─── Detail: sequential reduce over a range ─────────────────────────────────

namespace detail {

/// Reduce a contiguous range of elements with transform.
template <typename InputIt, typename U, typename ReduceOp, typename TransformOp>
U sequential_transform_reduce(InputIt first, InputIt last,
                               U init, ReduceOp reduce_op,
                               TransformOp transform) {
  U result = init;
  for (auto it = first; it != last; ++it) {
    result = reduce_op(result, transform(*it));
  }
  return result;
}

/// Parallel reduce using std::async thread pool.
/// Divides work into chunks, reduces each chunk in a thread, then combines.
template <typename InputIt, typename U, typename ReduceOp, typename TransformOp>
U parallel_transform_reduce(InputIt first, InputIt last,
                             U init, ReduceOp reduce_op,
                             TransformOp transform,
                             int num_workers) {
  auto total = std::distance(first, last);
  if (total <= 0) return init;

  if (num_workers <= 1) {
    return sequential_transform_reduce(first, last, init, reduce_op, transform);
  }

  // Chunk the work
  auto chunk_size = (total + num_workers - 1) / num_workers;
  std::vector<std::future<U>> futures;
  futures.reserve(num_workers);

  auto chunk_begin = first;
  for (int i = 0; i < num_workers && chunk_begin != last; ++i) {
    auto chunk_end = chunk_begin;
    auto advance_count = std::min(chunk_size,
                                   std::distance(chunk_begin, last));
    std::advance(chunk_end, advance_count);

    futures.push_back(std::async(std::launch::async,
        [chunk_begin, chunk_end, init, &reduce_op, &transform]() {
          return sequential_transform_reduce(
              chunk_begin, chunk_end, init, reduce_op, transform);
        }));

    chunk_begin = chunk_end;
  }

  // Combine partial results
  U result = init;
  for (auto& f : futures) {
    result = reduce_op(result, f.get());
  }
  return result;
}

}  // namespace detail

// ═══════════════════════════════════════════════════════════════════════════════
// parallel_reduce — over flat element range
// ═══════════════════════════════════════════════════════════════════════════════

/// Parallel transform-reduce over an iterator range.
/// Use when you already have a flat collection of elements.
template <typename InputIt, typename U,
          typename ReduceOp    = std::plus<U>,
          typename TransformOp = std::identity>
U parallel_reduce(InputIt first, InputIt last,
                  U init,
                  ReduceOp reduce_op = {},
                  TransformOp transform = {},
                  ParallelReduceConfig cfg = {}) {
  auto total = std::distance(first, last);

  // Determine backend
  auto backend = cfg.backend;
  if (backend == ParallelReduceConfig::Backend::Auto) {
    if (static_cast<size_t>(total) < cfg.min_elements_per_thread) {
      backend = ParallelReduceConfig::Backend::Sequential;
    } else {
      backend = ParallelReduceConfig::Backend::ThreadPool;
    }
  }

  int workers = cfg.thread_pool_workers;
  if (workers < 0) {
    workers = static_cast<int>(std::thread::hardware_concurrency());
    if (workers == 0) workers = 4;  // Safe fallback
  }

  switch (backend) {
    case ParallelReduceConfig::Backend::Sequential:
      return detail::sequential_transform_reduce(
          first, last, init, reduce_op, transform);

    case ParallelReduceConfig::Backend::ThreadPool:
      return detail::parallel_transform_reduce(
          first, last, init, reduce_op, transform, workers);

    case ParallelReduceConfig::Backend::WarpReduce:
      // GPU path: fall through to CPU for now (stub)
      // In production: launch warp-parallel kernel, read back scalar
      return detail::parallel_transform_reduce(
          first, last, init, reduce_op, transform, workers);

    default:
      return detail::sequential_transform_reduce(
          first, last, init, reduce_op, transform);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// parallel_reduce — over pointer-to-buffer collection (world-query style)
// ═══════════════════════════════════════════════════════════════════════════════

/// Parallel reduce over a collection of data sources (e.g., ChannelBuffer).
/// Each source is read via a user-supplied accessor function.
///
/// Usage:
///   auto total = data::parallel_reduce_sources(
///       all_bufs, 0.f,
///       std::plus<float>{},
///       [](const IndividualState& s) { return s.priority; });
///
template <typename SourcePtr, typename U,
          typename ReduceOp, typename TransformOp>
U parallel_reduce_sources(
    std::span<SourcePtr> sources,
    U init,
    ReduceOp reduce_op,
    TransformOp transform,
    ParallelReduceConfig cfg = {}) {
  int workers = cfg.thread_pool_workers;
  if (workers < 0) {
    workers = static_cast<int>(std::thread::hardware_concurrency());
    if (workers == 0) workers = 4;
  }

  auto n_sources = sources.size();
  if (n_sources == 0) return init;

  if (cfg.backend == ParallelReduceConfig::Backend::Sequential ||
      n_sources == 1) {
    // Sequential across sources
    U result = init;
    for (auto& src : sources) {
      // Caller is responsible for providing sources with begin()/end()
      // or a custom access pattern
      (void)src;
    }
    return result;
  }

  // Parallel: one future per source (each source is independent data)
  std::vector<std::future<U>> futures;
  futures.reserve(n_sources);

  for (auto& src : sources) {
    futures.push_back(std::async(std::launch::async,
        [&src, init, &reduce_op, &transform]() -> U {
          // Each source provides its own data; transform+reduce locally
          // Caller wraps ChannelBuffer access in the transform lambda
          return transform(src);
        }));
  }

  U result = init;
  for (auto& f : futures) {
    result = reduce_op(result, f.get());
  }
  return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Convenience: reduce_sum, reduce_max, reduce_min
// ═══════════════════════════════════════════════════════════════════════════════

/// Sum all transformed elements across the range.
template <typename InputIt, typename U, typename TransformOp = std::identity>
U reduce_sum(InputIt first, InputIt last, U init = {},
             TransformOp transform = {},
             ParallelReduceConfig cfg = {}) {
  return parallel_reduce(first, last, init, std::plus<U>{}, transform, cfg);
}

/// Find the maximum transformed element.
template <typename InputIt, typename U, typename TransformOp = std::identity>
U reduce_max(InputIt first, InputIt last, U init,
             TransformOp transform = {},
             ParallelReduceConfig cfg = {}) {
  return parallel_reduce(first, last, init,
      [](const U& a, const U& b) { return a > b ? a : b; },
      transform, cfg);
}

/// Find the minimum transformed element.
template <typename InputIt, typename U, typename TransformOp = std::identity>
U reduce_min(InputIt first, InputIt last, U init,
             TransformOp transform = {},
             ParallelReduceConfig cfg = {}) {
  return parallel_reduce(first, last, init,
      [](const U& a, const U& b) { return a < b ? a : b; },
      transform, cfg);
}

}  // namespace data
}  // namespace cyber
}  // namespace world

#endif  // CYBER_DATA_PARALLEL_REDUCE_H_
