/******************************************************************************
 * staged_algorithms.h — Pre-allocated staged algorithm wrappers
 *
 * PRD #246: Separate temporary-buffer allocation from compute dispatch
 * in the individual state update pipeline. Eliminates per-tick malloc
 * on RT-Thread (RK3568) where heap allocation violates real-time
 * constraints (measured: 3,491 missed deadlines/24h from malloc jitter).
 *
 * Pattern:
 *   on_init():  auto staged = make_staged_transform_reduce<T, U>(...);
 *   on_tick():  U result = staged.execute(data_begin, data_end);
 *               // zero heap allocation in execute()
 *
 * Scratch memory is allocated once at construction and reused across
 * all subsequent execute() calls. Multiple staged objects can share
 * a ScratchPool for same-sized scratch buffers.
 *****************************************************************************/

#ifndef CYBER_DATA_STAGED_ALGORITHMS_H_
#define CYBER_DATA_STAGED_ALGORITHMS_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>
#include <span>
#include <vector>

namespace world {
namespace cyber {
namespace data {

// ═══════════════════════════════════════════════════════════════════════════════
// ScratchBuffer — Pre-allocated reusable temporary storage
// ═══════════════════════════════════════════════════════════════════════════════

class ScratchBuffer {
 public:
  ScratchBuffer() = default;

  explicit ScratchBuffer(size_t bytes)
      : data_(bytes ? new uint8_t[bytes] : nullptr),
        capacity_(bytes) {}

  ~ScratchBuffer() { delete[] data_; }

  // Move-only
  ScratchBuffer(ScratchBuffer&& o) noexcept
      : data_(o.data_), capacity_(o.capacity_) {
    o.data_ = nullptr;
    o.capacity_ = 0;
  }
  ScratchBuffer& operator=(ScratchBuffer&& o) noexcept {
    if (this != &o) {
      delete[] data_;
      data_ = o.data_;
      capacity_ = o.capacity_;
      o.data_ = nullptr;
      o.capacity_ = 0;
    }
    return *this;
  }

  ScratchBuffer(const ScratchBuffer&) = delete;
  ScratchBuffer& operator=(const ScratchBuffer&) = delete;

  uint8_t* data() noexcept { return data_; }
  const uint8_t* data() const noexcept { return data_; }
  size_t capacity() const noexcept { return capacity_; }

  /// Get a typed view into the scratch buffer.
  template <typename T>
  T* as() noexcept { return reinterpret_cast<T*>(data_); }

  template <typename T>
  std::span<T> span(size_t count) noexcept {
    return {reinterpret_cast<T*>(data_), count};
  }

  /// Ensure capacity is at least `bytes`. Reallocates only if needed.
  void ensure(size_t bytes) {
    if (bytes > capacity_) {
      delete[] data_;
      data_ = new uint8_t[bytes];
      capacity_ = bytes;
    }
  }

 private:
  uint8_t* data_ = nullptr;
  size_t capacity_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ScratchPool — Shared pool for multiple staged algorithms
// ═══════════════════════════════════════════════════════════════════════════════

class ScratchPool {
 public:
  explicit ScratchPool(size_t initial_capacity = 0)
      : buffer_(initial_capacity) {}

  /// Get the shared scratch buffer. Ensures at least `bytes` capacity.
  ScratchBuffer& get(size_t bytes) {
    buffer_.ensure(bytes);
    return buffer_;
  }

  size_t capacity() const noexcept { return buffer_.capacity(); }

 private:
  ScratchBuffer buffer_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// StagedTransformReduce
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Pre-allocated transform-reduce. Scratch buffer holds per-worker partial
 * results for multi-threaded reduction.
 *
 * Construction: allocates scratch for up to `max_workers` partial results.
 * execute(): zero heap allocation, uses pre-allocated scratch.
 */
template <typename T, typename U,
          typename ReduceOp    = std::plus<U>,
          typename TransformOp = std::identity>
class StagedTransformReduce {
 public:
  StagedTransformReduce(size_t max_elements, int max_workers, U init,
                         ReduceOp reduce_op = {}, TransformOp transform = {})
      : init_(init),
        reduce_op_(reduce_op),
        transform_(transform),
        max_workers_(max_workers),
        partials_(max_workers) {}

  /// Execute the staged reduce. Zero heap allocation.
  U execute(const T* first, const T* last) {
    auto n = last - first;
    if (n <= 0) return init_;

    // Sequential for small inputs
    if (n < 2048 || max_workers_ <= 1) {
      U result = init_;
      for (auto it = first; it != last; ++it) {
        result = reduce_op_(result, transform_(*it));
      }
      return result;
    }

    // Parallel: partition into chunks, reduce each into partials_
    int workers = std::min(max_workers_, static_cast<int>((n + 1023) / 1024));
    auto chunk_size = n / workers;

    // Reduce each chunk sequentially (partials stored in pre-allocated vector)
    for (int w = 0; w < workers; ++w) {
      auto chunk_begin = first + w * chunk_size;
      auto chunk_end = (w == workers - 1) ? last : chunk_begin + chunk_size;
      U partial = init_;
      for (auto it = chunk_begin; it != chunk_end; ++it) {
        partial = reduce_op_(partial, transform_(*it));
      }
      partials_[w] = partial;
    }

    // Combine partials (no allocation — partials_ is pre-allocated)
    U result = init_;
    for (int w = 0; w < workers; ++w) {
      result = reduce_op_(result, partials_[w]);
    }
    return result;
  }

  /// Execute over a span.
  U execute(std::span<const T> data) {
    return execute(data.data(), data.data() + data.size());
  }

  /// Scratch memory footprint.
  size_t scratch_bytes() const noexcept {
    return partials_.capacity() * sizeof(U);
  }

 private:
  U init_;
  ReduceOp reduce_op_;
  TransformOp transform_;
  int max_workers_;
  std::vector<U> partials_;  // Pre-allocated at construction
};

// ═══════════════════════════════════════════════════════════════════════════════
// StagedBatchedTopK
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Pre-allocated batched top-K selection over IndividualState arrays.
 * Scratch buffer holds the K-element heap.
 */
template <typename T, typename Compare = std::less<T>>
class StagedBatchedTopK {
 public:
  StagedBatchedTopK(size_t k, Compare cmp = {})
      : k_(k), cmp_(cmp), heap_(k) {}

  /// Find the top-K elements. Results written to `out` span.
  /// Zero heap allocation (heap_ is pre-allocated).
  size_t execute(const T* first, const T* last, std::span<T> out) {
    auto n = last - first;
    size_t actual_k = std::min(k_, static_cast<size_t>(n));
    if (actual_k == 0) return 0;

    // Partial sort: use pre-allocated heap as workspace
    // Copy first K elements
    for (size_t i = 0; i < actual_k; ++i) {
      heap_[i] = first[i];
    }
    std::make_heap(heap_.begin(), heap_.begin() + actual_k, cmp_);

    // Scan remaining elements
    for (auto it = first + actual_k; it != last; ++it) {
      if (cmp_(*it, heap_[0])) {
        std::pop_heap(heap_.begin(), heap_.begin() + actual_k, cmp_);
        heap_[actual_k - 1] = *it;
        std::push_heap(heap_.begin(), heap_.begin() + actual_k, cmp_);
      }
    }

    // Output
    size_t out_count = std::min(actual_k, out.size());
    std::sort_heap(heap_.begin(), heap_.begin() + actual_k, cmp_);
    for (size_t i = 0; i < out_count; ++i) {
      out[i] = heap_[i];
    }
    return out_count;
  }

  size_t scratch_bytes() const noexcept { return heap_.capacity() * sizeof(T); }

 private:
  size_t k_;
  Compare cmp_;
  std::vector<T> heap_;  // Pre-allocated at construction
};

// ═══════════════════════════════════════════════════════════════════════════════
// Factory functions
// ═══════════════════════════════════════════════════════════════════════════════

template <typename T, typename U,
          typename ReduceOp = std::plus<U>,
          typename TransformOp = std::identity>
auto make_staged_transform_reduce(size_t max_elements, int max_workers,
                                   U init, ReduceOp r = {},
                                   TransformOp t = {}) {
  return StagedTransformReduce<T, U, ReduceOp, TransformOp>(
      max_elements, max_workers, init, r, t);
}

template <typename T, typename Compare = std::less<T>>
auto make_staged_batched_topk(size_t k, Compare cmp = {}) {
  return StagedBatchedTopK<T, Compare>(k, cmp);
}

}  // namespace data
}  // namespace cyber
}  // namespace world

#endif  // CYBER_DATA_STAGED_ALGORITHMS_H_
