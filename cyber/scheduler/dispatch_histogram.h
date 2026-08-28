/******************************************************************************
 * dispatch_histogram.h — Low-contention task-dispatch latency histogram
 *
 * PRD #232: Replace shared-atomic histogram with thread-local accumulation
 * + periodic flush.  Hot path is a pure local write (no atomics, no lock).
 *
 * Bucket layout: 64 buckets, logarithmic spacing from 0ns to ~67ms.
 *   bucket 0  = [0, 16) ns
 *   bucket k  = [16 * 2^(k/4), 16 * 2^((k+1)/4)) ns
 *   bucket 63 = [16 * 2^(63/4), ∞) ns
 *
 * Memory: 64 × 8 bytes per thread + 64 × 8 bytes global = ~512B/thread.
 *****************************************************************************/

#ifndef CYBER_SCHEDULER_DISPATCH_HISTOGRAM_H_
#define CYBER_SCHEDULER_DISPATCH_HISTOGRAM_H_

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace world {
namespace scheduler {

enum class FlushStrategy : uint8_t {
  kEveryNTicks      = 0,   // Flush every N record() calls (default)
  kEveryMs          = 1,   // Flush every M milliseconds
  kOnProfilerQuery  = 2,   // Flush only when profiler reads
};

class DispatchHistogram {
 public:
  static constexpr int kBuckets = 64;
  static constexpr uint64_t kFlushInterval = 1024;  // Power of 2 for fast modulo

  // ── Thread-local accumulator ──────────────────────────────────────────────

  struct alignas(64) LocalHistogram {
    uint64_t buckets[kBuckets] = {};
    uint64_t tick_count = 0;

    void reset() {
      std::memset(buckets, 0, sizeof(buckets));
      tick_count = 0;
    }
  };

  // ── Hot path: record one dispatch latency ─────────────────────────────────

  void record(std::chrono::nanoseconds latency) {
    int bucket = latency_to_bucket(latency.count());
    auto& local = local_histogram();
    local.buckets[bucket]++;
    // Flush every kFlushInterval records (& mask, no modulo division)
    if ((++local.tick_count & (kFlushInterval - 1)) == 0) {
      flush_local(local);
    }
  }

  // ── Global snapshot (profiler, low frequency) ─────────────────────────────

  std::array<uint64_t, kBuckets> snapshot() const {
    std::array<uint64_t, kBuckets> result;
    for (int i = 0; i < kBuckets; ++i) {
      result[i] = global_buckets_[i].load(std::memory_order_relaxed);
    }
    return result;
  }

  // ── Force-flush the calling thread's local and return snapshot ─────────

  std::array<uint64_t, kBuckets> snapshot_flushed() {
    flush_local(local_histogram());
    return snapshot();
  }

  // ── Reset everything ──────────────────────────────────────────────────────

  void reset() {
    for (int i = 0; i < kBuckets; ++i) {
      global_buckets_[i].store(0, std::memory_order_relaxed);
    }
    local_histogram().reset();
  }

  // ── Bucket boundaries (for reporting/rendering) ───────────────────────────

  static uint64_t bucket_lower_bound_ns(int bucket) {
    if (bucket <= 0) return 0;
    // Logarithmic: 16 * 2^(bucket/4)
    double exponent = static_cast<double>(bucket) / 4.0;
    return static_cast<uint64_t>(16.0 * std::pow(2.0, exponent));
  }

  static int latency_to_bucket(int64_t ns) {
    if (ns <= 0) return 0;
    if (ns < 16) return 0;
    // Inverse of 16 * 2^(k/4): k = 4 * log2(ns/16)
    double k = 4.0 * std::log2(static_cast<double>(ns) / 16.0);
    int bucket = static_cast<int>(k);
    if (bucket >= kBuckets) return kBuckets - 1;
    return bucket;
  }

 private:
  void flush_local(LocalHistogram& local) {
    for (int i = 0; i < kBuckets; ++i) {
      if (local.buckets[i] > 0) {
        global_buckets_[i].fetch_add(local.buckets[i],
                                     std::memory_order_relaxed);
        local.buckets[i] = 0;
      }
    }
  }

  // Thread-local accessor
  static LocalHistogram& local_histogram() {
    thread_local LocalHistogram tl;
    return tl;
  }

  std::array<std::atomic<uint64_t>, kBuckets> global_buckets_{};
};

// Global instance
inline DispatchHistogram g_dispatch_histogram;

}  // namespace scheduler
}  // namespace world

#endif  // CYBER_SCHEDULER_DISPATCH_HISTOGRAM_H_
