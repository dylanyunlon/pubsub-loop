/******************************************************************************
 * writer_core.h — Unified individual-to-individual state writer
 *
 * PRD #0: Unify RTPS and shared-memory transport backends into a single
 * individual-to-individual state delivery pipeline.
 *
 * Key properties:
 *   - Single write() call dispatches to optimal backend (RTPS/SHM/Intra)
 *   - Runtime backend migration without dropping messages
 *   - QoS enforcement with ≤200ns overhead per write
 *   - Metrics collection for latency/throughput monitoring
 *****************************************************************************/

#ifndef CYBER_TRANSPORT_WRITER_CORE_H_
#define CYBER_TRANSPORT_WRITER_CORE_H_

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "cyber/base/signal.h"
#include "cyber/common/log.h"
#include "cyber/transport/common/identity.h"
#include "cyber/transport/message/message_info.h"
#include "cyber/transport/qos/qos_profile_conf.h"
#include "cyber/transport/transmitter/hybrid_transmitter.h"

namespace world {
namespace cyber {
namespace transport {

/**
 * Transport backend selection
 */
enum class BackendHint : uint8_t {
  kAuto = 0,       // Let topology decide
  kIntraProcess,   // Same process — zero-copy
  kSharedMemory,   // Same machine — mmap
  kRtps,           // Cross-network — DDS/RTPS
};

/**
 * Per-write metrics (lock-free atomic counters)
 */
struct WriterMetrics {
  std::atomic<uint64_t> writes_total{0};
  std::atomic<uint64_t> writes_shm{0};
  std::atomic<uint64_t> writes_rtps{0};
  std::atomic<uint64_t> writes_intra{0};
  std::atomic<uint64_t> write_errors{0};
  std::atomic<uint64_t> qos_drops{0};
  std::atomic<uint64_t> total_latency_ns{0};  // Cumulative for avg
  std::atomic<uint64_t> max_latency_ns{0};
  std::atomic<uint64_t> migrations{0};

  void record_write(BackendHint backend, uint64_t latency_ns) {
    writes_total.fetch_add(1, std::memory_order_relaxed);
    total_latency_ns.fetch_add(latency_ns, std::memory_order_relaxed);

    // Update max atomically
    uint64_t current_max = max_latency_ns.load(std::memory_order_relaxed);
    while (latency_ns > current_max &&
           !max_latency_ns.compare_exchange_weak(
               current_max, latency_ns, std::memory_order_relaxed)) {}

    switch (backend) {
      case BackendHint::kSharedMemory:
        writes_shm.fetch_add(1, std::memory_order_relaxed); break;
      case BackendHint::kRtps:
        writes_rtps.fetch_add(1, std::memory_order_relaxed); break;
      case BackendHint::kIntraProcess:
        writes_intra.fetch_add(1, std::memory_order_relaxed); break;
      default: break;
    }
  }

  double avg_latency_ns() const {
    uint64_t total = writes_total.load(std::memory_order_relaxed);
    return total > 0
        ? static_cast<double>(total_latency_ns.load(std::memory_order_relaxed)) / total
        : 0.0;
  }
};

/**
 * QoS enforcer — depth limiting + sequence numbering
 * Target: ≤200ns overhead per write
 */
class QosEnforcer {
 public:
  explicit QosEnforcer(const QosProfile& profile)
      : depth_(profile.depth()), reliability_(profile.reliability()) {}

  bool should_drop(uint64_t seq) const {
    // Simple depth-based: if queue depth exceeded, signal drop
    // Real implementation hooks into HistoryCache
    return false;  // Placeholder — full QoS in history_cache.h
  }

  uint64_t next_seq() {
    return seq_.fetch_add(1, std::memory_order_relaxed);
  }

 private:
  uint32_t depth_;
  QosReliabilityPolicy reliability_;
  std::atomic<uint64_t> seq_{0};
};

/**
 * WriterCore<T> — the unified writer
 *
 * Usage:
 *   WriterCore<IndividualState> writer(attr, participant);
 *   writer.Enable();
 *   writer.Write(state_msg);
 *   auto& m = writer.metrics();  // introspect
 */
template <typename MessageT>
class WriterCore {
 public:
  using MessagePtr = std::shared_ptr<MessageT>;

  WriterCore(const proto::RoleAttributes& attr,
             const ParticipantPtr& participant)
      : attr_(attr), participant_(participant) {
    // Initialize QoS from attributes
    qos_ = std::make_unique<QosEnforcer>(attr.qos_profile());

    // Create underlying hybrid transmitter (handles RTPS/SHM/Intra)
    transmitter_ = std::make_shared<HybridTransmitter<MessageT>>(
        attr, participant);
  }

  ~WriterCore() { Disable(); }

  void Enable() {
    transmitter_->Enable();
    enabled_.store(true, std::memory_order_release);
  }

  void Disable() {
    enabled_.store(false, std::memory_order_release);
    transmitter_->Disable();
  }

  /**
   * Write a message — the hot path. Must be ≤200ns QoS overhead.
   */
  bool Write(const MessagePtr& msg) {
    if (!enabled_.load(std::memory_order_acquire)) return false;

    auto t0 = std::chrono::steady_clock::now();

    // QoS enforcement
    uint64_t seq = qos_->next_seq();
    if (qos_->should_drop(seq)) {
      metrics_.qos_drops.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    // Build message info
    MessageInfo msg_info;
    msg_info.set_seq_num(seq);
    msg_info.set_channel_id(attr_.channel_id());

    // Transmit through hybrid backend
    bool ok = transmitter_->Transmit(msg, msg_info);
    if (!ok) {
      metrics_.write_errors.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    auto t1 = std::chrono::steady_clock::now();
    uint64_t lat_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        t1 - t0).count();
    metrics_.record_write(current_backend_, lat_ns);

    return true;
  }

  /**
   * Request backend migration (e.g., when topology changes)
   */
  void MigrateBackend(BackendHint hint) {
    std::lock_guard<std::mutex> lock(migration_mutex_);
    current_backend_ = hint;
    metrics_.migrations.fetch_add(1, std::memory_order_relaxed);
    // The actual migration is handled by HybridTransmitter's
    // internal mode switching when topology events arrive
    AINFO << "WriterCore backend migration to "
          << static_cast<int>(hint);
  }

  const WriterMetrics& metrics() const { return metrics_; }
  bool enabled() const { return enabled_.load(std::memory_order_acquire); }
  const proto::RoleAttributes& attributes() const { return attr_; }

 private:
  proto::RoleAttributes attr_;
  ParticipantPtr participant_;
  std::unique_ptr<QosEnforcer> qos_;
  std::shared_ptr<HybridTransmitter<MessageT>> transmitter_;

  WriterMetrics metrics_;
  std::atomic<bool> enabled_{false};
  BackendHint current_backend_{BackendHint::kAuto};
  std::mutex migration_mutex_;
};

}  // namespace transport
}  // namespace cyber
}  // namespace world

#endif  // CYBER_TRANSPORT_WRITER_CORE_H_
