/******************************************************************************
 * atomic.h — Guarded transport::Atomic<T> wrapper
 *
 * PRD #30: Enable safe inclusion of transport headers in translation units
 * that lack atomic hardware support (codegen cross-targets, docgen, sysmo).
 *
 * When PUBSUB_LOOP_HAS_ATOMICS is defined and non-zero:
 *   transport::Atomic<T> = std::atomic<T>  (full lock-free semantics)
 *
 * When PUBSUB_LOOP_HAS_ATOMICS is 0 or undefined:
 *   transport::Atomic<T> = non-thread-safe stub (layout-compatible,
 *   build-tool safe, runtime-use guarded by static_assert)
 *
 * Sources:
 *   - Apollo CyberRT: transport layer (unconditional std::atomic)
 *   - pubsub-loop: governance.h, writer_core.h (existing atomic usage)
 *
 * Namespace: world::cyber::transport
 *****************************************************************************/

#ifndef CYBER_TRANSPORT_ATOMIC_H_
#define CYBER_TRANSPORT_ATOMIC_H_

// Default: assume atomics are available on the current platform.
// Build systems can override this to 0 for cross-compile build tools.
#ifndef PUBSUB_LOOP_HAS_ATOMICS
  #if defined(__STDC_NO_ATOMICS__) || \
      (defined(__riscv) && !defined(__riscv_atomic))
    #define PUBSUB_LOOP_HAS_ATOMICS 0
  #else
    #define PUBSUB_LOOP_HAS_ATOMICS 1
  #endif
#endif

// Runtime build flag — set to 0 for build-time tools
#ifndef PUBSUB_LOOP_RUNTIME_BUILD
  #define PUBSUB_LOOP_RUNTIME_BUILD PUBSUB_LOOP_HAS_ATOMICS
#endif

#if PUBSUB_LOOP_HAS_ATOMICS

#include <atomic>

namespace world {
namespace cyber {
namespace transport {

template <typename T>
using Atomic = std::atomic<T>;

// Memory order aliases (match std:: naming for grep-ability)
constexpr auto kRelaxed = std::memory_order_relaxed;
constexpr auto kAcquire = std::memory_order_acquire;
constexpr auto kRelease = std::memory_order_release;
constexpr auto kAcqRel  = std::memory_order_acq_rel;
constexpr auto kSeqCst  = std::memory_order_seq_cst;

}  // namespace transport
}  // namespace cyber
}  // namespace world

#define PUBSUB_LOOP_TRANSPORT_LOCKFREE 1

#else  // !PUBSUB_LOOP_HAS_ATOMICS

namespace world {
namespace cyber {
namespace transport {

/**
 * Stub Atomic<T> — layout-compatible with std::atomic<T>.
 *
 * NOT thread-safe. Usable ONLY in build-time tools (codegen, docgen)
 * and static analysis contexts where the header is included for type
 * metadata but no runtime atomic operations are performed.
 *
 * Any attempt to instantiate this in a runtime build triggers a
 * compile-time error.
 */
template <typename T>
struct Atomic {
  static_assert(
      PUBSUB_LOOP_RUNTIME_BUILD == 0,
      "transport::Atomic<T> stub instantiated in a runtime build. "
      "Define PUBSUB_LOOP_HAS_ATOMICS=1 for targets with hardware "
      "atomic support, or set PUBSUB_LOOP_RUNTIME_BUILD=0 for "
      "build-time tool translation units.");

  T value_{};

  Atomic() noexcept = default;
  explicit Atomic(T v) noexcept : value_(v) {}

  T load(int /*order*/ = 0) const noexcept { return value_; }
  void store(T v, int /*order*/ = 0) noexcept { value_ = v; }

  T exchange(T desired, int /*order*/ = 0) noexcept {
    T old = value_;
    value_ = desired;
    return old;
  }

  T fetch_add(T arg, int /*order*/ = 0) noexcept {
    T old = value_;
    value_ += arg;
    return old;
  }

  T fetch_sub(T arg, int /*order*/ = 0) noexcept {
    T old = value_;
    value_ -= arg;
    return old;
  }

  bool compare_exchange_weak(T& expected, T desired,
                             int /*success*/ = 0,
                             int /*failure*/ = 0) noexcept {
    if (value_ == expected) {
      value_ = desired;
      return true;
    }
    expected = value_;
    return false;
  }

  bool compare_exchange_strong(T& expected, T desired,
                               int /*success*/ = 0,
                               int /*failure*/ = 0) noexcept {
    return compare_exchange_weak(expected, desired);
  }

  operator T() const noexcept { return value_; }
};

// Placeholder memory order values for non-atomic builds
constexpr int kRelaxed = 0;
constexpr int kAcquire = 0;
constexpr int kRelease = 0;
constexpr int kAcqRel  = 0;
constexpr int kSeqCst  = 0;

}  // namespace transport
}  // namespace cyber
}  // namespace world

#define PUBSUB_LOOP_TRANSPORT_LOCKFREE 0

#endif  // PUBSUB_LOOP_HAS_ATOMICS

#endif  // CYBER_TRANSPORT_ATOMIC_H_
