/******************************************************************************
 * distributed_core.h — DistributedCore transport configuration
 *
 * PRD #454: Cleaned config struct — 14 actionable fields (removed 9 phantom
 * fields that were parsed but never applied). Platform-specific extensions
 * are compile-guarded behind WORLD_PLATFORM_X86.
 *
 * validate_config() throws InvalidConfigError at construction time instead
 * of silently falling through to incorrect transport settings at runtime.
 *****************************************************************************/

#ifndef CYBER_IO_DISTRIBUTED_CORE_H_
#define CYBER_IO_DISTRIBUTED_CORE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "cyber/transport/policy_selector.h"

namespace world {
namespace cyber {
namespace io {

using transport::TransportBackend;
using transport::QoSReliability;
using transport::QoSHistory;

// ─── InvalidConfigError ─────────────────────────────────────────────────────

class InvalidConfigError : public std::invalid_argument {
 public:
  using std::invalid_argument::invalid_argument;
};

// ─── Platform-specific extensions ───────────────────────────────────────────

#if defined(WORLD_PLATFORM_X86)
/// CPU affinity mask — bitfield up to 256 cores
struct CpuAffinityMask {
  uint64_t bits[4] = {};

  void set(int core) {
    if (core >= 0 && core < 256) {
      bits[core / 64] |= (1ULL << (core % 64));
    }
  }
  bool test(int core) const {
    if (core < 0 || core >= 256) return false;
    return (bits[core / 64] >> (core % 64)) & 1;
  }
  void clear() { bits[0] = bits[1] = bits[2] = bits[3] = 0; }
};

struct X86Extension {
  bool enable_cpu_affinity = false;
  CpuAffinityMask affinity_mask;
  bool numa_aware = false;
  bool shm_huge_page = false;
};
#endif

// ─── DistributedCoreConfig ──────────────────────────────────────────────────

struct DistributedCoreConfig {
  // ── Transport backend selection ──
  TransportBackend backend = TransportBackend::kRtpsUdp;
  bool prefer_zero_copy = false;

  // ── DDS/RTPS QoS (all fields have real effect) ──
  QoSReliability dds_reliability = QoSReliability::kBestEffort;
  QoSHistory dds_history = QoSHistory::kKeepLastN;
  uint32_t dds_depth = 1;
  // REMOVED: dds_durability — use DDS default VOLATILE; file separate FEA if needed
  // REMOVED: dds_ownership_exclusive — always SHARED in pub/sub-loop model

  // ── SHM ──
  size_t shm_segment_size = 1024 * 1024;  // 1 MB default
  uint32_t shm_reader_count = 1;
  // REMOVED: shm_huge_page — moved to X86Extension
  // REMOVED: shm_topology — RING_BUFFER is the only impl

  // ── Intra-process ──
  bool intraprocess_enabled = true;
  size_t intraprocess_pool_size = 64;

  // ── Platform-specific extensions ──
#if defined(WORLD_PLATFORM_X86)
  std::optional<X86Extension> x86;
#endif
};

// ─── validate_config ────────────────────────────────────────────────────────

/// Validates a DistributedCoreConfig at construction time.
/// Throws InvalidConfigError for any invalid combination.
/// Must complete in < 1ms — no I/O, no allocation.
inline void validate_config(const DistributedCoreConfig& cfg) {
  // SHM segment size must be positive
  if (cfg.shm_segment_size == 0) {
    throw InvalidConfigError(
        "DistributedCoreConfig: shm_segment_size must be > 0");
  }

  // SHM backend requires at least one reader
  if ((cfg.backend == TransportBackend::kSharedMemory) &&
      cfg.shm_reader_count == 0) {
    throw InvalidConfigError(
        "DistributedCoreConfig: SHM backend requires shm_reader_count > 0");
  }

  // DDS depth must be positive
  if (cfg.dds_depth == 0) {
    throw InvalidConfigError(
        "DistributedCoreConfig: dds_depth must be > 0");
  }

  // Intra-process pool must be > 0 if enabled
  if (cfg.intraprocess_enabled && cfg.intraprocess_pool_size == 0) {
    throw InvalidConfigError(
        "DistributedCoreConfig: intraprocess_pool_size must be > 0 "
        "when intraprocess_enabled is true");
  }

#if defined(WORLD_PLATFORM_X86)
  // If x86 extension is set, validate it
  if (cfg.x86.has_value()) {
    const auto& ext = cfg.x86.value();
    if (ext.shm_huge_page &&
        cfg.backend != TransportBackend::kSharedMemory) {
      throw InvalidConfigError(
          "DistributedCoreConfig: shm_huge_page requires SHM backend");
    }
  }
#endif
}

// ─── DistributedCore ────────────────────────────────────────────────────────

/// Transport negotiation and backend selection.
/// Called during IndividualExecutionContext::initialize() and on every
/// node reconnect.
class DistributedCore {
 public:
  explicit DistributedCore(const DistributedCoreConfig& config)
      : config_(config) {
    validate_config(config_);
  }

  /// Negotiate transport for an individual node.
  /// Returns the selected backend (may differ from config if a backend
  /// is unavailable at runtime).
  TransportBackend negotiate_transport() const {
    // If prefer_zero_copy and intraprocess is available, prefer it
    if (config_.prefer_zero_copy && config_.intraprocess_enabled) {
      return TransportBackend::kIntraProcess;
    }
    return config_.backend;
  }

  const DistributedCoreConfig& config() const { return config_; }

 private:
  DistributedCoreConfig config_;
};

}  // namespace io
}  // namespace cyber
}  // namespace world

#endif  // CYBER_IO_DISTRIBUTED_CORE_H_
