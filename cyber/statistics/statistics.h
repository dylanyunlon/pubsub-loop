/******************************************************************************
 * statistics.h — World runtime statistics and latency sampling
 *
 * Ported from: apollo/cyber/statistics/statistics.h
 * Changes:
 *   - Removed bvar dependency (not available outside baidu infra)
 *   - Uses simple atomic counters + ring-buffer for latency tracking
 *   - Singleton pattern preserved
 *
 * Namespace: world::cyber::statistics
 *****************************************************************************/

#ifndef CYBER_STATISTICS_STATISTICS_H_
#define CYBER_STATISTICS_STATISTICS_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "cyber/common/log.h"
#include "cyber/common/macros.h"

// World snapshot analysis (independent headers, does not break existing API)
// #include "cyber/statistics/world_snapshot_analyzer.h"  // include as needed

namespace world {
namespace cyber {
namespace statistics {

/// Lightweight latency histogram (lock-free per-channel)
struct LatencyVar {
  std::atomic<uint64_t> count{0};
  std::atomic<uint64_t> sum_ns{0};
  std::atomic<uint64_t> min_ns{UINT64_MAX};
  std::atomic<uint64_t> max_ns{0};

  void Record(uint64_t sample_ns) {
    count.fetch_add(1, std::memory_order_relaxed);
    sum_ns.fetch_add(sample_ns, std::memory_order_relaxed);
    // CAS loop for min/max
    uint64_t cur = min_ns.load(std::memory_order_relaxed);
    while (sample_ns < cur &&
           !min_ns.compare_exchange_weak(cur, sample_ns,
                                         std::memory_order_relaxed)) {}
    cur = max_ns.load(std::memory_order_relaxed);
    while (sample_ns > cur &&
           !max_ns.compare_exchange_weak(cur, sample_ns,
                                         std::memory_order_relaxed)) {}
  }

  double MeanNs() const {
    uint64_t c = count.load(std::memory_order_relaxed);
    return c > 0 ? static_cast<double>(sum_ns.load(std::memory_order_relaxed))
                       / static_cast<double>(c)
                 : 0.0;
  }
};

/// Per-channel status value (e.g., processing latency, queue depth)
struct StatusVar {
  std::atomic<uint64_t> value{0};
};

/// Simple span tracker for profiling
struct SpanTracker {
  uint64_t start_ns{0};
  uint64_t min_ns{0};
  bool active{false};
};

class Statistics {
 public:
  ~Statistics() = default;

  void DisableChanVar() { disabled_ = true; }
  bool IsDisabled() const { return disabled_; }

  /// Record a processing latency sample for a channel.
  /// channel_key = "node_name-channel_name"
  template <typename SampleT>
  bool SamplingProcLatency(const std::string& channel_key, SampleT sample_ns) {
    if (disabled_) return true;
    GetOrCreate(proc_vars_, channel_key)->Record(
        static_cast<uint64_t>(sample_ns));
    return true;
  }

  /// Record a transport latency sample.
  template <typename SampleT>
  bool SamplingTranLatency(const std::string& channel_key, SampleT sample_ns) {
    if (disabled_) return true;
    if (channel_key.find("_timer_component") != std::string::npos) return true;
    GetOrCreate(tran_vars_, channel_key)->Record(
        static_cast<uint64_t>(sample_ns));
    return true;
  }

  /// Record end-to-end cyber latency.
  template <typename SampleT>
  bool SamplingCyberLatency(const std::string& channel_key,
                            SampleT sample_ns) {
    if (disabled_) return true;
    if (channel_key.find("_timer_component") != std::string::npos) return true;
    GetOrCreate(cyber_vars_, channel_key)->Record(
        static_cast<uint64_t>(sample_ns));
    return true;
  }

  /// Set a status value for a channel.
  bool SetProcStatus(const std::string& channel_key, uint64_t val) {
    if (disabled_) return true;
    GetOrCreateStatus(status_vars_, channel_key)->value.store(
        val, std::memory_order_relaxed);
    return true;
  }

  /// Create a named span for profiling.
  bool CreateSpan(const std::string& name, uint64_t min_ns = 0) {
    std::lock_guard<std::mutex> lock(span_mu_);
    spans_[name] = SpanTracker{0, min_ns, false};
    return true;
  }

  bool StartSpan(const std::string& name) {
    std::lock_guard<std::mutex> lock(span_mu_);
    auto it = spans_.find(name);
    if (it == spans_.end()) return false;
    it->second.start_ns = NowNs();
    it->second.active = true;
    return true;
  }

  bool EndSpan(const std::string& name) {
    std::lock_guard<std::mutex> lock(span_mu_);
    auto it = spans_.find(name);
    if (it == spans_.end() || !it->second.active) return false;
    it->second.active = false;
    uint64_t elapsed = NowNs() - it->second.start_ns;
    if (elapsed >= it->second.min_ns) {
      AINFO << "Span [" << name << "] " << elapsed / 1000 << " us";
    }
    return true;
  }

  /// Get latency stats for a channel.
  const LatencyVar* GetProcStats(const std::string& key) const {
    auto it = proc_vars_.find(key);
    return it != proc_vars_.end() ? it->second.get() : nullptr;
  }

 private:
  bool disabled_ = false;

  std::mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<LatencyVar>> proc_vars_;
  std::unordered_map<std::string, std::shared_ptr<LatencyVar>> tran_vars_;
  std::unordered_map<std::string, std::shared_ptr<LatencyVar>> cyber_vars_;
  std::unordered_map<std::string, std::shared_ptr<StatusVar>> status_vars_;

  std::mutex span_mu_;
  std::unordered_map<std::string, SpanTracker> spans_;

  std::shared_ptr<LatencyVar> GetOrCreate(
      std::unordered_map<std::string, std::shared_ptr<LatencyVar>>& map,
      const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto& ptr = map[key];
    if (!ptr) ptr = std::make_shared<LatencyVar>();
    return ptr;
  }

  std::shared_ptr<StatusVar> GetOrCreateStatus(
      std::unordered_map<std::string, std::shared_ptr<StatusVar>>& map,
      const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto& ptr = map[key];
    if (!ptr) ptr = std::make_shared<StatusVar>();
    return ptr;
  }

  static uint64_t NowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
  }

  DECLARE_SINGLETON(Statistics)
};

}  // namespace statistics
}  // namespace cyber
}  // namespace world

#endif  // CYBER_STATISTICS_STATISTICS_H_
