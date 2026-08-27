/******************************************************************************
 * backend_router.h — Runtime transport backend selection and migration
 *
 * PRD #0: BackendRouter migration protocol
 *
 * Routes individual state channels to optimal backend based on:
 *   - Topology distance (same process → Intra, same node → SHM, remote → RTPS)
 *   - Message size (small → RTPS ok, large → prefer SHM)
 *   - QoS requirements (reliable → RTPS, best-effort → SHM)
 *   - Migration: seamless switch without message loss
 *****************************************************************************/

#ifndef CYBER_TRANSPORT_BACKEND_ROUTER_H_
#define CYBER_TRANSPORT_BACKEND_ROUTER_H_

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "cyber/base/signal.h"
#include "cyber/common/log.h"
#include "cyber/proto/transport_conf.pb.h"
#include "cyber/service_discovery/specific_manager/channel_manager.h"
#include "cyber/transport/writer_core.h"

namespace world {
namespace cyber {
namespace transport {

/**
 * Topology distance between two endpoints
 */
enum class TopologyDistance : uint8_t {
  kSameProcess = 0,   // Intra-process
  kSameMachine = 1,   // Shared memory
  kRemote = 2,        // RTPS/Network
  kUnknown = 3,
};

/**
 * Migration event — emitted when a channel switches backend
 */
struct MigrationEvent {
  std::string channel_name;
  BackendHint from;
  BackendHint to;
  uint64_t timestamp_ns;
};

/**
 * BackendRouter — decides transport backend per channel
 */
class BackendRouter {
 public:
  using MigrationCallback = std::function<void(const MigrationEvent&)>;

  BackendRouter() = default;
  ~BackendRouter() = default;

  /**
   * Select optimal backend for a channel given current topology
   */
  BackendHint SelectBackend(const std::string& channel_name,
                            size_t avg_msg_size,
                            const proto::QosProfile& qos) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if we have topology info
    auto it = topology_.find(channel_name);
    TopologyDistance dist = (it != topology_.end())
        ? it->second
        : TopologyDistance::kUnknown;

    // Decision logic:
    // 1. Same process → always Intra (zero-copy, ~10ns)
    // 2. Same machine + msg > 256 bytes → SHM (~100ns)
    // 3. Same machine + small msg → SHM or Intra depending on reader count
    // 4. Remote → RTPS (~200µs but necessary)
    // 5. Unknown → RTPS (safe default)

    switch (dist) {
      case TopologyDistance::kSameProcess:
        return BackendHint::kIntraProcess;

      case TopologyDistance::kSameMachine:
        return BackendHint::kSharedMemory;

      case TopologyDistance::kRemote:
        return BackendHint::kRtps;

      case TopologyDistance::kUnknown:
      default:
        // Conservative: start with RTPS, migrate when topology known
        return BackendHint::kRtps;
    }
  }

  /**
   * Update topology distance for a channel (called by service_discovery)
   */
  void UpdateTopology(const std::string& channel_name,
                      TopologyDistance distance) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto old = topology_[channel_name];
    topology_[channel_name] = distance;

    // If distance changed, emit migration event
    if (old != distance) {
      BackendHint from = DistanceToHint(old);
      BackendHint to = DistanceToHint(distance);
      
      MigrationEvent event{channel_name, from, to,
          static_cast<uint64_t>(
              std::chrono::steady_clock::now().time_since_epoch().count())};
      
      migration_signal_(event);
    }
  }

  /**
   * Subscribe to migration events
   */
  void OnMigration(const MigrationCallback& cb) {
    migration_signal_.Connect(cb);
  }

 private:
  static BackendHint DistanceToHint(TopologyDistance d) {
    switch (d) {
      case TopologyDistance::kSameProcess: return BackendHint::kIntraProcess;
      case TopologyDistance::kSameMachine: return BackendHint::kSharedMemory;
      case TopologyDistance::kRemote: return BackendHint::kRtps;
      default: return BackendHint::kAuto;
    }
  }

  std::mutex mutex_;
  std::unordered_map<std::string, TopologyDistance> topology_;
  base::Signal<const MigrationEvent&> migration_signal_;
};

}  // namespace transport
}  // namespace cyber
}  // namespace world

#endif  // CYBER_TRANSPORT_BACKEND_ROUTER_H_
