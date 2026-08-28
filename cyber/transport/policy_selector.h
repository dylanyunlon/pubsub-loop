/******************************************************************************
 * policy_selector.h — Compile-time + runtime transport delivery policy tuner.
 *
 * Given a description of the individual state being transported and the
 * communication backend, selects an optimal TransportDeliveryPolicy.
 * Design mirrors CCCL's cub::policy_selector pattern (constexpr operator()
 * that takes a capability struct and returns a fully specified policy).
 *
 * PRD: #1446 Transport policy_selector DSL
 *****************************************************************************/
#ifndef CYBER_TRANSPORT_POLICY_SELECTOR_H_
#define CYBER_TRANSPORT_POLICY_SELECTOR_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace world {
namespace cyber {
namespace transport {

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------
enum class TransportBackend : uint8_t {
  kIntraProcess  = 0,
  kSharedMemory  = 1,
  kRtpsUdp       = 2,
  kRtpsTcp       = 3,
};

enum class QoSReliability : uint8_t {
  kBestEffort = 0,
  kReliable   = 1,
};

enum class QoSHistory : uint8_t {
  kKeepLastN = 0,
  kKeepAll   = 1,
};

enum class BackpressureStrategy : uint8_t {
  kBlock         = 0,
  kDropOldest    = 1,
  kAdaptiveRate  = 2,
};

enum class SerializationMethod : uint8_t {
  kZeroCopyMove   = 0,  // intra-process: move pointer
  kZeroCopyAtomic = 1,  // single-word atomic store in SHM
  kZeroCopyMemcpy = 2,  // multi-word memcpy in SHM region
  kCdrAligned     = 3,  // CDR serialization (RTPS / non-trivial)
  kProtobuf       = 4,  // protobuf for complex types
};

enum class PollingStrategy : uint8_t {
  kBusyWait           = 0,
  kExponentialBackoff = 1,
  kFixedDelay         = 2,
  kConditionVariable  = 3,
};

// ---------------------------------------------------------------------------
// Policy output
// ---------------------------------------------------------------------------
struct TransportDeliveryPolicy {
  int                   batch_size           = 1;
  int                   ring_buffer_depth    = 4;
  BackpressureStrategy  backpressure         = BackpressureStrategy::kDropOldest;
  SerializationMethod   serialization        = SerializationMethod::kCdrAligned;
  PollingStrategy       polling              = PollingStrategy::kFixedDelay;
  int                   polling_delay_ns     = 500;
};

// ---------------------------------------------------------------------------
// Capability descriptor (runtime hardware / transport constraints)
// ---------------------------------------------------------------------------
struct TransportCapability {
  int  max_message_size       = 65536;  // MTU or SHM segment ceiling
  int  max_concurrent_writers = 256;
  bool supports_rdma          = false;
  int  l2_cache_size          = 0;      // bytes; 0 = unknown
};

// ---------------------------------------------------------------------------
// Policy selector — constexpr callable
// ---------------------------------------------------------------------------
struct PolicySelector {
  // --- State descriptor (compile-time or runtime) ---
  int  individual_state_size       = 128;
  int  individual_state_alignment  = 8;
  bool trivially_copyable          = true;
  bool is_primitive                = false;

  // --- Transport descriptor ---
  TransportBackend backend         = TransportBackend::kRtpsUdp;
  QoSReliability   reliability     = QoSReliability::kBestEffort;
  QoSHistory       history         = QoSHistory::kKeepLastN;
  int              history_depth   = 1;

  // --- Topology ---
  int  publisher_count             = 1;
  int  subscriber_count            = 1;
  bool supports_zero_copy          = false;

  /// Select the optimal delivery policy for given transport capabilities.
  constexpr TransportDeliveryPolicy operator()(
      TransportCapability cap) const {

    // --- Intra-process: zero-copy pointer move ---
    if (backend == TransportBackend::kIntraProcess && trivially_copyable) {
      return {
        .batch_size        = std::max(1, 4096 / std::max(individual_state_size, 1)),
        .ring_buffer_depth = std::max(2, publisher_count * 2),
        .backpressure      = BackpressureStrategy::kBlock,
        .serialization     = SerializationMethod::kZeroCopyMove,
        .polling           = PollingStrategy::kBusyWait,
        .polling_delay_ns  = 0,
      };
    }

    // --- Shared memory: single-word atomic path ---
    if (backend == TransportBackend::kSharedMemory &&
        supports_zero_copy && trivially_copyable &&
        individual_state_size <= 16 &&
        (individual_state_size & (individual_state_size - 1)) == 0) {
      return {
        .batch_size        = 1,
        .ring_buffer_depth = 2,
        .backpressure      = BackpressureStrategy::kDropOldest,
        .serialization     = SerializationMethod::kZeroCopyAtomic,
        .polling           = PollingStrategy::kExponentialBackoff,
        .polling_delay_ns  = 100,
      };
    }

    // --- Shared memory: multi-word zero-copy ---
    if (backend == TransportBackend::kSharedMemory &&
        supports_zero_copy && trivially_copyable) {
      return {
        .batch_size        = std::max(1, cap.max_message_size /
                                 std::max(individual_state_size + 4, 1)),
        .ring_buffer_depth = 4,
        .backpressure      = BackpressureStrategy::kAdaptiveRate,
        .serialization     = SerializationMethod::kZeroCopyMemcpy,
        .polling           = PollingStrategy::kExponentialBackoff,
        .polling_delay_ns  = 200,
      };
    }

    // --- Shared memory: non-trivial types ---
    if (backend == TransportBackend::kSharedMemory) {
      return {
        .batch_size        = std::max(1, 2048 / std::max(individual_state_size, 1)),
        .ring_buffer_depth = 8,
        .backpressure      = BackpressureStrategy::kAdaptiveRate,
        .serialization     = SerializationMethod::kCdrAligned,
        .polling           = PollingStrategy::kFixedDelay,
        .polling_delay_ns  = 500,
      };
    }

    // --- RTPS reliable with many subscribers ---
    if ((backend == TransportBackend::kRtpsUdp ||
         backend == TransportBackend::kRtpsTcp) &&
        reliability == QoSReliability::kReliable &&
        subscriber_count > 10) {
      return {
        .batch_size        = std::max(1, 1024 / std::max(individual_state_size, 1)),
        .ring_buffer_depth = std::max(history_depth, 16),
        .backpressure      = BackpressureStrategy::kAdaptiveRate,
        .serialization     = individual_state_size <= 64 && trivially_copyable
                                 ? SerializationMethod::kCdrAligned
                                 : SerializationMethod::kProtobuf,
        .polling           = PollingStrategy::kConditionVariable,
        .polling_delay_ns  = 1000,
      };
    }

    // --- RTPS default path ---
    return {
      .batch_size        = 1,
      .ring_buffer_depth = std::max(history_depth, 4),
      .backpressure      = reliability == QoSReliability::kReliable
                               ? BackpressureStrategy::kBlock
                               : BackpressureStrategy::kDropOldest,
      .serialization     = trivially_copyable && individual_state_size <= 256
                               ? SerializationMethod::kCdrAligned
                               : SerializationMethod::kProtobuf,
      .polling           = PollingStrategy::kConditionVariable,
      .polling_delay_ns  = 500,
    };
  }
};

// ---------------------------------------------------------------------------
// Convenience: type-level policy selection (compile-time)
// ---------------------------------------------------------------------------
template <typename IndividualStateT,
          TransportBackend Backend = TransportBackend::kRtpsUdp>
struct StaticPolicySelector {
  static constexpr PolicySelector selector{
      .individual_state_size      = static_cast<int>(sizeof(IndividualStateT)),
      .individual_state_alignment = static_cast<int>(alignof(IndividualStateT)),
      .trivially_copyable         = std::is_trivially_copyable_v<IndividualStateT>,
      .is_primitive               = std::is_arithmetic_v<IndividualStateT>,
      .backend                    = Backend,
      .supports_zero_copy         = (Backend == TransportBackend::kIntraProcess ||
                                     Backend == TransportBackend::kSharedMemory),
  };

  static constexpr TransportDeliveryPolicy select(TransportCapability cap) {
    return selector(cap);
  }
};

}  // namespace transport
}  // namespace cyber
}  // namespace world

#endif  // CYBER_TRANSPORT_POLICY_SELECTOR_H_
