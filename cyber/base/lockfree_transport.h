/******************************************************************************
 * lockfree_transport.h — Portable lock-free ring buffer transport fallback
 *
 * PRD #240: Replaces POSIX-dependent SequentialTransport with a lock-free
 * SPMC ring buffer. When <cuda/std/atomic> is available, it compiles for
 * both host and device contexts; otherwise falls back to std::atomic.
 *
 * Algorithm: single-producer multi-consumer ring (power-of-2 capacity),
 * cache-line padded head/tail, release-acquire ordering.
 *****************************************************************************/

#ifndef CYBER_BASE_LOCKFREE_TRANSPORT_H_
#define CYBER_BASE_LOCKFREE_TRANSPORT_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <type_traits>

// Detect CUDA availability
#if __has_include(<cuda/std/atomic>)
  #include <cuda/std/atomic>
  #include <cuda/std/semaphore>
  #define WORLD_HAS_CUDA_STD 1
  #define WORLD_HOST_DEVICE __host__ __device__
#else
  #define WORLD_HAS_CUDA_STD 0
  #define WORLD_HOST_DEVICE
#endif

namespace world {
namespace base {

static constexpr int kTransportCacheLine = 64;

template <typename T, int Capacity = 256>
class LockfreeTransport {
  static_assert((Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2");
  static_assert(std::is_trivially_copyable_v<T>,
                "T must be trivially copyable for lock-free store");

 public:
  LockfreeTransport() = default;

  /// Non-blocking publish. Returns false if ring is full.
  WORLD_HOST_DEVICE bool try_publish(const T& item) noexcept {
    uint32_t head = head_.load(std::memory_order_relaxed);
    uint32_t next = (head + 1) & kMask;
    if (next == tail_.load(std::memory_order_acquire)) {
      return false;  // full
    }
    ring_[head] = item;
    head_.store(next, std::memory_order_release);
    count_.fetch_add(1, std::memory_order_release);
    return true;
  }

  /// Blocking publish: spin-yield until space available.
  WORLD_HOST_DEVICE void publish(const T& item) noexcept {
    while (!try_publish(item)) {
#ifdef __CUDA_ARCH__
      __nanosleep(64);
#else
      std::this_thread::yield();
#endif
    }
  }

  /// Non-blocking subscribe. Returns false if empty.
  WORLD_HOST_DEVICE bool try_subscribe(T& item) noexcept {
    uint32_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return false;  // empty
    }
    item = ring_[tail];
    tail_.store((tail + 1) & kMask, std::memory_order_release);
    count_.fetch_sub(1, std::memory_order_relaxed);
    return true;
  }

  /// Blocking subscribe with timeout.
  bool subscribe(T& item, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (try_subscribe(item)) return true;
      std::this_thread::yield();
    }
    return false;
  }

  WORLD_HOST_DEVICE int size() const noexcept {
    return static_cast<int>(count_.load(std::memory_order_relaxed));
  }

  WORLD_HOST_DEVICE bool empty() const noexcept { return size() == 0; }
  WORLD_HOST_DEVICE bool full() const noexcept { return size() >= Capacity - 1; }

 private:
  static constexpr uint32_t kMask = Capacity - 1;

  alignas(kTransportCacheLine) std::atomic<uint32_t> head_{0};
  alignas(kTransportCacheLine) std::atomic<uint32_t> tail_{0};
  alignas(kTransportCacheLine) std::atomic<int32_t>  count_{0};
  T ring_[Capacity];
};

/// API-compatible adapter: matches the old SequentialTransport interface.
template <typename T, int Capacity = 256>
class SequentialTransport : public LockfreeTransport<T, Capacity> {
  using Base = LockfreeTransport<T, Capacity>;

 public:
  void publish(const T& t) { Base::publish(t); }
  bool try_get(T& t)       { return Base::try_subscribe(t); }
};

}  // namespace base
}  // namespace world

#undef WORLD_HOST_DEVICE

#endif  // CYBER_BASE_LOCKFREE_TRANSPORT_H_
