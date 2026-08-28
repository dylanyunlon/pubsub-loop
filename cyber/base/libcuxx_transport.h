/******************************************************************************
 * libcuxx_transport.h — Lock-free ring buffer transport (portable)
 *
 * PRD #240: Replaces the POSIX-dependent SequentialTransport with a
 * lock-free SPSC ring buffer using atomic operations. Works on:
 *   - Linux x86/ARM (std::atomic)
 *   - CUDA device code (cuda::std::atomic when __CUDACC__ is defined)
 *   - Windows MSVC (std::atomic, no POSIX dependency)
 *   - Embedded / constrained environments (no heap allocation)
 *
 * Design:
 *   - Statically allocated ring buffer (power-of-2 capacity)
 *   - Single-producer single-consumer (SPSC) by default
 *   - acquire/release memory ordering (not seq_cst — saves fence on ARM)
 *   - Zero heap allocation (entire structure fits in static storage)
 *
 * For CUDA device code, define WORLD_USE_LIBCUXX to use cuda::std::atomic
 * instead of std::atomic. The API is identical either way.
 *****************************************************************************/

#ifndef CYBER_BASE_LIBCUXX_TRANSPORT_H_
#define CYBER_BASE_LIBCUXX_TRANSPORT_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <type_traits>

// ─── Atomic type selection ──────────────────────────────────────────────────

#if defined(__CUDACC__) && defined(WORLD_USE_LIBCUXX)
  #include <cuda/std/atomic>
  #define WORLD_ATOMIC(T) cuda::std::atomic<T>
  #define WORLD_MEM_RELAXED cuda::std::memory_order_relaxed
  #define WORLD_MEM_ACQUIRE cuda::std::memory_order_acquire
  #define WORLD_MEM_RELEASE cuda::std::memory_order_release
  #define WORLD_HOST_DEVICE __host__ __device__
#else
  #define WORLD_ATOMIC(T) std::atomic<T>
  #define WORLD_MEM_RELAXED std::memory_order_relaxed
  #define WORLD_MEM_ACQUIRE std::memory_order_acquire
  #define WORLD_MEM_RELEASE std::memory_order_release
  #define WORLD_HOST_DEVICE
#endif

namespace world {
namespace cyber {
namespace base {

/**
 * Lock-free SPSC ring buffer transport.
 *
 * @tparam T        Element type (must be trivially copyable)
 * @tparam Capacity Ring buffer size (must be power of 2)
 */
template <typename T, int Capacity = 256>
class LibcuxxTransport {
  static_assert((Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2");
  static_assert(Capacity >= 2,
                "Capacity must be at least 2");
  static_assert(std::is_trivially_copyable_v<T>,
                "T must be trivially copyable for lock-free transport");

 public:
  static constexpr int kCapacity = Capacity;
  static constexpr uint32_t kMask = static_cast<uint32_t>(Capacity - 1);

  LibcuxxTransport() = default;

  // Non-copyable, non-movable (contains atomics)
  LibcuxxTransport(const LibcuxxTransport&) = delete;
  LibcuxxTransport& operator=(const LibcuxxTransport&) = delete;

  // ── Non-blocking publish ──

  /**
   * Try to publish an item. Returns false if ring is full.
   * Lock-free, wait-free. Safe to call from __device__ code.
   */
  WORLD_HOST_DEVICE bool try_publish(const T& item) noexcept {
    uint32_t head = head_.load(WORLD_MEM_RELAXED);
    uint32_t next = (head + 1) & kMask;

    if (next == tail_.load(WORLD_MEM_ACQUIRE)) {
      return false;  // Full
    }

    ring_[head] = item;
    head_.store(next, WORLD_MEM_RELEASE);
    publish_count_.fetch_add(1, WORLD_MEM_RELAXED);
    return true;
  }

  // ── Blocking publish (host only) ──

  /**
   * Publish with spin-wait until space is available.
   */
  void publish(const T& item) noexcept {
    while (!try_publish(item)) {
#if defined(__CUDA_ARCH__)
      __nanosleep(64);
#else
      std::this_thread::yield();
#endif
    }
  }

  // ── Non-blocking subscribe ──

  /**
   * Try to consume an item. Returns false if ring is empty.
   * Lock-free, wait-free.
   */
  WORLD_HOST_DEVICE bool try_subscribe(T& item) noexcept {
    uint32_t tail = tail_.load(WORLD_MEM_RELAXED);

    if (tail == head_.load(WORLD_MEM_ACQUIRE)) {
      return false;  // Empty
    }

    item = ring_[tail];
    tail_.store((tail + 1) & kMask, WORLD_MEM_RELEASE);
    return true;
  }

  // ── Blocking subscribe with timeout (host only) ──

  /**
   * Wait for an item with timeout. Returns false on timeout.
   */
  bool subscribe(T& item,
                 std::chrono::milliseconds timeout
                     = std::chrono::milliseconds(100)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (try_subscribe(item)) return true;
      std::this_thread::yield();
    }
    return false;
  }

  // ── Drain: consume all available items ──

  /**
   * Drain up to max_items from the ring. Returns number consumed.
   */
  template <typename OutputIt>
  int drain(OutputIt out, int max_items = Capacity) noexcept {
    int count = 0;
    T item;
    while (count < max_items && try_subscribe(item)) {
      *out++ = item;
      ++count;
    }
    return count;
  }

  // ── Introspection ──

  WORLD_HOST_DEVICE int size() const noexcept {
    uint32_t h = head_.load(WORLD_MEM_RELAXED);
    uint32_t t = tail_.load(WORLD_MEM_RELAXED);
    return static_cast<int>((h - t) & kMask);
  }

  WORLD_HOST_DEVICE bool empty() const noexcept {
    return head_.load(WORLD_MEM_RELAXED) == tail_.load(WORLD_MEM_RELAXED);
  }

  WORLD_HOST_DEVICE bool full() const noexcept {
    uint32_t next = (head_.load(WORLD_MEM_RELAXED) + 1) & kMask;
    return next == tail_.load(WORLD_MEM_RELAXED);
  }

  uint64_t total_published() const noexcept {
    return publish_count_.load(WORLD_MEM_RELAXED);
  }

  /// Reset to empty state. NOT thread-safe — call only when no
  /// concurrent publish/subscribe is in progress.
  void reset() noexcept {
    head_.store(0, WORLD_MEM_RELAXED);
    tail_.store(0, WORLD_MEM_RELAXED);
    publish_count_.store(0, WORLD_MEM_RELAXED);
  }

 private:
  // Cache-line pad head and tail to avoid false sharing
  alignas(64) WORLD_ATOMIC(uint32_t) head_{0};
  alignas(64) WORLD_ATOMIC(uint32_t) tail_{0};
  alignas(64) WORLD_ATOMIC(uint64_t) publish_count_{0};

  T ring_[Capacity];
};

}  // namespace base
}  // namespace cyber
}  // namespace world

// Cleanup macros
#undef WORLD_ATOMIC
#undef WORLD_MEM_RELAXED
#undef WORLD_MEM_ACQUIRE
#undef WORLD_MEM_RELEASE
#undef WORLD_HOST_DEVICE

#endif  // CYBER_BASE_LIBCUXX_TRANSPORT_H_
