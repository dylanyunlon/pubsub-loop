/******************************************************************************
 * world_engine.h — Central world tick engine
 *
 * The CyberRT "physics engine" analogue:
 *   1. Collect all MotionRequests from individuals
 *   2. Run governance filter
 *   3. Resolve collisions / constraints
 *   4. Publish ConfirmedState to each individual
 *   5. Broadcast WorldTick + WorldStateSnapshot
 *
 * "只要动了就需要申请说自己要动了" — this is objective.
 * The engine doesn't push to individuals; individuals must apply to move,
 * and the world confirms or denies.
 *****************************************************************************/

#ifndef CYBER_COMPONENT_WORLD_ENGINE_H_
#define CYBER_COMPONENT_WORLD_ENGINE_H_

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "cyber/base/bounded_queue.h"
#include "cyber/base/signal.h"
#include "cyber/common/log.h"
#include "cyber/node/node.h"
#include "cyber/transport/backend_router.h"
#include "cyber/transport/governance.h"
#include "cyber/transport/individual_state.h"
#include "cyber/transport/writer_core.h"

namespace world {
namespace cyber {
namespace component {

using transport::BackendHint;
using transport::GovernanceFilter;
using transport::GovernanceResult;
using transport::IndividualId;
using transport::IndividualState;
using transport::TrustRing;
using transport::Vec3f;
using transport::Quatf;

/**
 * Registered individual descriptor
 */
struct IndividualRecord {
  IndividualId id;
  std::string name;
  uint64_t collision_mask;
  float mass;
  float radius;

  // Current confirmed state
  Vec3f position{0, 0, 0};
  Quatf orientation{1, 0, 0, 0};
  Vec3f velocity{0, 0, 0};

  // Trust level
  TrustRing trust{TrustRing::kUser};
};

/**
 * Pending motion request (collected per tick)
 */
struct PendingMotion {
  IndividualId id;
  Vec3f delta_position;
  Quatf delta_rotation;
  Vec3f velocity_intent;
  uint64_t collision_mask;
  uint32_t priority;
  uint64_t submit_ns;     // When request was submitted
};

/**
 * Collision pair detected during resolution
 */
struct CollisionPair {
  IndividualId a;
  IndividualId b;
  float penetration;
  Vec3f normal;
};

/**
 * WorldEngine — the central authority
 *
 * Usage:
 *   WorldEngine engine("my_world");
 *   engine.RegisterIndividual({id, "agent_0", ...});
 *   engine.Start(30.0);  // 30 Hz tick
 *   // ... individuals submit requests via channels ...
 *   engine.Stop();
 */
class WorldEngine {
 public:
  explicit WorldEngine(const std::string& world_name)
      : world_name_(world_name) {
    governance_ = std::make_unique<GovernanceFilter>();
  }

  ~WorldEngine() { Stop(); }

  /**
   * Register an individual into the world
   */
  bool RegisterIndividual(IndividualRecord record) {
    std::lock_guard<std::mutex> lock(individuals_mutex_);
    
    if (individuals_.count(record.id.value)) {
      AERROR << "Duplicate individual ID: " << record.id.value;
      return false;
    }

    governance_->SetTrust(record.id.value, record.trust);
    individuals_[record.id.value] = std::move(record);

    AINFO << "Registered individual " << record.id.value
          << " (" << record.name << ")";
    return true;
  }

  /**
   * Unregister an individual
   */
  void UnregisterIndividual(IndividualId id) {
    std::lock_guard<std::mutex> lock(individuals_mutex_);
    individuals_.erase(id.value);
  }

  /**
   * Submit a motion request (called by individual's channel callback)
   */
  void SubmitMotion(PendingMotion motion) {
    pending_motions_.Enqueue(std::move(motion));
  }

  /**
   * Start the tick loop at given frequency
   */
  void Start(double tick_hz = 30.0) {
    if (running_.exchange(true)) return;
    
    tick_period_ns_ = static_cast<uint64_t>(1e9 / tick_hz);
    tick_thread_ = std::thread([this]() { TickLoop(); });
    
    AINFO << "WorldEngine '" << world_name_ << "' started at "
          << tick_hz << " Hz";
  }

  /**
   * Stop the tick loop
   */
  void Stop() {
    if (!running_.exchange(false)) return;
    if (tick_thread_.joinable()) tick_thread_.join();
    AINFO << "WorldEngine '" << world_name_ << "' stopped";
  }

  /**
   * Run a single tick manually (for testing / external tick source)
   */
  void Tick(double dt) {
    uint64_t tick_id = tick_counter_.fetch_add(1, std::memory_order_relaxed);
    ProcessTick(tick_id, dt);
  }

  // Governance access
  GovernanceFilter& governance() { return *governance_; }

  // Metrics
  uint64_t tick_count() const {
    return tick_counter_.load(std::memory_order_relaxed);
  }
  size_t individual_count() const {
    std::lock_guard<std::mutex> lock(individuals_mutex_);
    return individuals_.size();
  }

  // Signals
  base::Signal<uint64_t, double>& on_tick() { return on_tick_signal_; }
  base::Signal<const IndividualState&>& on_state_confirmed() {
    return on_state_confirmed_signal_;
  }
  base::Signal<const CollisionPair&>& on_collision() {
    return on_collision_signal_;
  }

 private:
  void TickLoop() {
    while (running_.load(std::memory_order_acquire)) {
      auto t0 = std::chrono::steady_clock::now();
      
      double dt = tick_period_ns_ * 1e-9;
      uint64_t tick_id = tick_counter_.fetch_add(1, std::memory_order_relaxed);
      
      ProcessTick(tick_id, dt);

      // Sleep until next tick
      auto t1 = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0);
      auto remaining = std::chrono::nanoseconds(tick_period_ns_) - elapsed;
      if (remaining.count() > 0) {
        std::this_thread::sleep_for(remaining);
      }
    }
  }

  void ProcessTick(uint64_t tick_id, double dt) {
    // 1. Emit tick signal
    on_tick_signal_(tick_id, dt);

    // 2. Collect pending motions
    std::vector<PendingMotion> motions;
    PendingMotion m;
    while (pending_motions_.Dequeue(&m)) {
      motions.push_back(std::move(m));
    }

    // Sort by priority (higher first)
    std::sort(motions.begin(), motions.end(),
              [](const PendingMotion& a, const PendingMotion& b) {
                return a.priority > b.priority;
              });

    // 3. Process each motion request
    std::lock_guard<std::mutex> lock(individuals_mutex_);
    std::vector<CollisionPair> collisions;

    for (auto& motion : motions) {
      auto it = individuals_.find(motion.id.value);
      if (it == individuals_.end()) continue;

      auto& individual = it->second;

      // 3a. Governance check
      GovernanceResult verdict = governance_->Evaluate(
          motion.id.value, &motion, sizeof(motion));

      if (verdict.decision == transport::Decision::kDeny) {
        // Publish denied state (position unchanged)
        EmitConfirmedState(individual, tick_id, false);
        continue;
      }

      // 3b. Apply motion (simple integration)
      Vec3f new_pos{
          individual.position.x + motion.delta_position.x,
          individual.position.y + motion.delta_position.y,
          individual.position.z + motion.delta_position.z,
      };

      // 3c. Collision detection (brute force AABB for now)
      bool collision = false;
      for (auto& [other_id, other] : individuals_) {
        if (other_id == motion.id.value) continue;
        if (!(motion.collision_mask & other.collision_mask)) continue;

        float dx = new_pos.x - other.position.x;
        float dy = new_pos.y - other.position.y;
        float dz = new_pos.z - other.position.z;
        float dist_sq = dx*dx + dy*dy + dz*dz;
        float min_dist = individual.radius + other.radius;

        if (dist_sq < min_dist * min_dist) {
          collision = true;
          float dist = std::sqrt(dist_sq);
          float penetration = min_dist - dist;
          Vec3f normal{0, 0, 0};
          if (dist > 1e-6f) {
            normal = {dx/dist, dy/dist, dz/dist};
          }
          collisions.push_back({motion.id, IndividualId(other_id),
                                penetration, normal});
        }
      }

      // 3d. Apply or reject based on collision
      if (!collision || verdict.decision == transport::Decision::kModify) {
        individual.position = new_pos;
        individual.velocity = motion.velocity_intent;
        EmitConfirmedState(individual, tick_id, true);
      } else {
        // Collision: don't move, report collision
        EmitConfirmedState(individual, tick_id, false);
      }
    }

    // 4. Emit collision events
    for (auto& col : collisions) {
      on_collision_signal_(col);
    }
  }

  void EmitConfirmedState(const IndividualRecord& ind, uint64_t tick_id,
                          bool granted) {
    IndividualState state{};
    state.individual_id = ind.id.value;
    state.tick_id = tick_id;
    state.position = ind.position;
    state.orientation = ind.orientation;
    state.velocity = ind.velocity;
    state.flags = granted ? 1 : 0;
    state.publish_ns = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    on_state_confirmed_signal_(state);
  }

  std::string world_name_;
  std::unique_ptr<GovernanceFilter> governance_;

  mutable std::mutex individuals_mutex_;
  std::unordered_map<uint64_t, IndividualRecord> individuals_;

  base::BoundedQueue<PendingMotion> pending_motions_{1048576};  // 1M slots

  std::atomic<uint64_t> tick_counter_{0};
  std::atomic<bool> running_{false};
  uint64_t tick_period_ns_{33333333};  // 30 Hz default
  std::thread tick_thread_;

  // Signals
  base::Signal<uint64_t, double> on_tick_signal_;
  base::Signal<const IndividualState&> on_state_confirmed_signal_;
  base::Signal<const CollisionPair&> on_collision_signal_;
};

}  // namespace component
}  // namespace cyber
}  // namespace world

#endif  // CYBER_COMPONENT_WORLD_ENGINE_H_
