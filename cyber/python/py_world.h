/******************************************************************************
 * py_world.h — pybind11 bridge for pubsub-loop WorldResolver
 *
 * Replaces apollo's py_cyber.h. Binds:
 *   - WorldResolver (individual registration, motion request, tick)
 *   - Node (pub/sub channels)
 *   - Writer/Reader (serialized proto transport)
 *   - Individual (motion request → confirmed state loop)
 *
 * Design: individuals submit MotionRequests to WorldResolver each tick.
 * WorldResolver resolves collisions / governance and publishes ConfirmedState.
 * This is the "动了就要申请" pattern — objective action notification.
 *****************************************************************************/

#ifndef CYBER_PYTHON_PY_WORLD_H_
#define CYBER_PYTHON_PY_WORLD_H_

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "cyber/common/global_data.h"
#include "cyber/common/log.h"
#include "cyber/message/raw_message.h"
#include "cyber/node/node.h"
#include "cyber/node/reader.h"
#include "cyber/node/writer.h"
#include "cyber/proto/world_state.pb.h"
#include "cyber/time/clock.h"
#include "cyber/transport/transport.h"

namespace world {
namespace cyber {

// Forward declarations
class PyWorldResolver;
class PyIndividual;

/**
 * PyWriter — thin wrapper for channel writing (same pattern as apollo)
 */
class PyWriter {
 public:
  PyWriter(const std::string& channel, const std::string& type,
           uint32_t qos_depth, Node* node)
      : channel_name_(channel), data_type_(type) {
    proto::RoleAttributes attr;
    attr.set_channel_name(channel);
    attr.set_message_type(type);
    attr.mutable_qos_profile()->set_depth(qos_depth);
    writer_ = node->CreateWriter<message::RawMessage>(attr);
  }

  int write(const std::string& data) {
    auto msg = std::make_shared<message::RawMessage>(data);
    return writer_->Write(msg);
  }

 private:
  std::string channel_name_;
  std::string data_type_;
  std::shared_ptr<Writer<message::RawMessage>> writer_;
};

/**
 * PyReader — thin wrapper for channel reading
 */
class PyReader {
 public:
  PyReader(const std::string& channel, const std::string& type, Node* node)
      : channel_name_(channel), data_type_(type) {
    reader_ = node->CreateReader<message::RawMessage>(
        channel, [this](const std::shared_ptr<const message::RawMessage>& msg) {
          this->cb(msg);
        });
  }

  std::string read(bool wait = false) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cache_.empty()) return "";
    std::string msg = cache_.front();
    cache_.pop_front();
    return msg;
  }

 private:
  void cb(const std::shared_ptr<const message::RawMessage>& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cache_.size() >= kMaxCacheSize) cache_.pop_front();
    cache_.push_back(msg->message);
  }

  static constexpr size_t kMaxCacheSize = 1024;
  std::string channel_name_;
  std::string data_type_;
  std::shared_ptr<Reader<message::RawMessage>> reader_;
  std::mutex mutex_;
  std::deque<std::string> cache_;
};

/**
 * PyIndividual — represents one entity in the world
 * 
 * The core "个体运动申请→世界确认" loop:
 *   1. Individual calls request_motion(dx, dy, dz, ...) 
 *   2. WorldResolver collects all requests for this tick
 *   3. WorldResolver resolves (collision, governance)
 *   4. Individual receives ConfirmedState via callback
 */
class PyIndividual {
 public:
  PyIndividual(uint64_t id, const std::string& name, Node* node)
      : individual_id_(id), name_(name), node_(node) {
    // Writer for motion requests on per-individual channel
    std::string req_channel = "/world/motion_request/" + std::to_string(id);
    motion_writer_ = std::make_unique<PyWriter>(
        req_channel, "world.cyber.proto.MotionRequest", 16, node);

    // Reader for confirmed state
    std::string state_channel = "/world/confirmed_state/" + std::to_string(id);
    state_reader_ = std::make_unique<PyReader>(
        state_channel, "world.cyber.proto.ConfirmedState", node);
  }

  /**
   * Submit motion request — "我要动了" (I'm going to move)
   * Returns serialized MotionRequest proto
   */
  int request_motion(double dx, double dy, double dz,
                     double dqx, double dqy, double dqz, double dqw,
                     double vx, double vy, double vz,
                     uint64_t collision_mask, uint32_t priority) {
    proto::MotionRequest req;
    req.set_individual_id(individual_id_);
    req.set_tick_id(current_tick_);
    req.set_dx(dx); req.set_dy(dy); req.set_dz(dz);
    req.set_dqx(dqx); req.set_dqy(dqy);
    req.set_dqz(dqz); req.set_dqw(dqw);
    req.set_vx(vx); req.set_vy(vy); req.set_vz(vz);
    req.set_collision_mask(collision_mask);
    req.set_priority(priority);
    req.set_issuer_node_id(node_->NodeId());
    req.set_timestamp_ns(Time::Now().ToNanosecond());

    std::string data;
    req.SerializeToString(&data);
    return motion_writer_->write(data);
  }

  /**
   * Read confirmed state — world's response after resolution
   */
  std::string read_confirmed_state() {
    return state_reader_->read(false);
  }

  uint64_t individual_id() const { return individual_id_; }
  const std::string& name() const { return name_; }
  void set_tick(uint64_t tick) { current_tick_ = tick; }

 private:
  uint64_t individual_id_;
  std::string name_;
  uint64_t current_tick_ = 0;
  Node* node_;
  std::unique_ptr<PyWriter> motion_writer_;
  std::unique_ptr<PyReader> state_reader_;
};

/**
 * PyWorldResolver — central authority that resolves all motion requests
 *
 * Pattern: every individual that moves must "申请" (apply/notify).
 * This is objective — not optional notification but physics-mandated.
 * Like PhysX: you submit forces, the engine resolves.
 */
class PyWorldResolver {
 public:
  PyWorldResolver(const std::string& world_name)
      : world_name_(world_name) {
    node_ = CreateNode(world_name);
    if (!node_) {
      AERROR << "Failed to create WorldResolver node: " << world_name;
      return;
    }

    // World tick broadcaster
    tick_writer_ = std::make_unique<PyWriter>(
        "/world/tick", "world.cyber.proto.WorldTick", 1, node_.get());

    // Bulk state snapshot publisher
    snapshot_writer_ = std::make_unique<PyWriter>(
        "/world/state_snapshot", "world.cyber.proto.WorldStateSnapshot",
        4, node_.get());
  }

  /**
   * Register an individual into the world
   */
  PyIndividual* register_individual(uint64_t id, const std::string& name) {
    auto individual = std::make_unique<PyIndividual>(id, name, node_.get());
    PyIndividual* ptr = individual.get();

    // Subscribe to this individual's motion requests
    std::string req_channel = "/world/motion_request/" + std::to_string(id);
    auto reader = std::make_unique<PyReader>(
        req_channel, "world.cyber.proto.MotionRequest", node_.get());
    
    std::lock_guard<std::mutex> lock(mutex_);
    individuals_[id] = std::move(individual);
    request_readers_[id] = std::move(reader);

    // Create confirmed state writer for this individual
    std::string state_channel = "/world/confirmed_state/" + std::to_string(id);
    state_writers_[id] = std::make_unique<PyWriter>(
        state_channel, "world.cyber.proto.ConfirmedState", 16, node_.get());

    return ptr;
  }

  /**
   * Tick the world — collect all motion requests, resolve, publish states
   */
  void tick(uint64_t tick_id, double dt) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Broadcast tick
    proto::WorldTick tick_msg;
    tick_msg.set_tick_id(tick_id);
    tick_msg.set_timestamp_ns(Time::Now().ToNanosecond());
    tick_msg.set_dt(dt);
    tick_msg.set_individual_count(individuals_.size());
    std::string tick_data;
    tick_msg.SerializeToString(&tick_data);
    tick_writer_->write(tick_data);

    // Update tick on all individuals
    for (auto& [id, ind] : individuals_) {
      ind->set_tick(tick_id);
    }

    // Collect motion requests
    std::vector<proto::MotionRequest> requests;
    for (auto& [id, reader] : request_readers_) {
      std::string data = reader->read(false);
      while (!data.empty()) {
        proto::MotionRequest req;
        if (req.ParseFromString(data)) {
          requests.push_back(std::move(req));
        }
        data = reader->read(false);
      }
    }

    // === RESOLVE (placeholder — real physics engine hooks here) ===
    // For now: grant all motions, apply delta, check simple AABB overlap
    proto::WorldStateSnapshot snapshot;
    snapshot.set_tick_id(tick_id);
    snapshot.set_timestamp_ns(Time::Now().ToNanosecond());

    for (auto& req : requests) {
      proto::ConfirmedState state;
      state.set_individual_id(req.individual_id());
      state.set_tick_id(tick_id);

      // Apply deltas (simple integration)
      auto it = positions_.find(req.individual_id());
      double x = 0, y = 0, z = 0;
      if (it != positions_.end()) {
        x = it->second[0]; y = it->second[1]; z = it->second[2];
      }
      x += req.dx(); y += req.dy(); z += req.dz();
      state.set_x(x); state.set_y(y); state.set_z(z);
      state.set_qx(req.dqx()); state.set_qy(req.dqy());
      state.set_qz(req.dqz()); state.set_qw(req.dqw());
      state.set_vx(req.vx()); state.set_vy(req.vy()); state.set_vz(req.vz());
      state.set_motion_granted(true);

      // Update stored position
      positions_[req.individual_id()] = {x, y, z};

      // Governance: allow all for now
      auto* verdict = state.mutable_verdict();
      verdict->set_decision(proto::GovernanceVerdict::ALLOW);

      // Publish confirmed state to individual's channel
      auto wit = state_writers_.find(req.individual_id());
      if (wit != state_writers_.end()) {
        std::string state_data;
        state.SerializeToString(&state_data);
        wit->second->write(state_data);
      }

      *snapshot.add_states() = state;
    }

    // Publish bulk snapshot
    std::string snap_data;
    snapshot.SerializeToString(&snap_data);
    snapshot_writer_->write(snap_data);
  }

  size_t individual_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return individuals_.size();
  }

  void shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    individuals_.clear();
    request_readers_.clear();
    state_writers_.clear();
    node_.reset();
  }

 private:
  std::string world_name_;
  std::unique_ptr<Node> node_;
  std::unique_ptr<PyWriter> tick_writer_;
  std::unique_ptr<PyWriter> snapshot_writer_;

  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, std::unique_ptr<PyIndividual>> individuals_;
  std::unordered_map<uint64_t, std::unique_ptr<PyReader>> request_readers_;
  std::unordered_map<uint64_t, std::unique_ptr<PyWriter>> state_writers_;
  std::unordered_map<uint64_t, std::array<double, 3>> positions_;
};

/**
 * PyNode — same as apollo's but holds WorldResolver reference
 */
class PyNode {
 public:
  explicit PyNode(const std::string& node_name) : node_name_(node_name) {
    node_ = CreateNode(node_name);
  }

  void shutdown() {
    node_.reset();
    AINFO << "PyNode " << node_name_ << " exit.";
  }

  PyWriter* create_writer(const std::string& channel, const std::string& type,
                          uint32_t qos_depth = 1) {
    if (node_) return new PyWriter(channel, type, qos_depth, node_.get());
    return nullptr;
  }

  PyReader* create_reader(const std::string& channel,
                          const std::string& type) {
    if (node_) return new PyReader(channel, type, node_.get());
    return nullptr;
  }

  PyIndividual* create_individual(uint64_t id, const std::string& name) {
    if (node_) return new PyIndividual(id, name, node_.get());
    return nullptr;
  }

 private:
  std::string node_name_;
  std::shared_ptr<Node> node_;
};

// Module-level functions
inline bool py_init(const std::string& module_name) {
  static bool inited = false;
  if (inited) return true;
  Init(module_name.c_str());
  inited = true;
  return true;
}

inline bool py_ok() { return OK(); }
inline void py_shutdown() { Clear(); }
inline bool py_is_shutdown() { return IsShutdown(); }
inline void py_waitforshutdown() { WaitForShutdown(); }

}  // namespace cyber
}  // namespace world

#endif  // CYBER_PYTHON_PY_WORLD_H_
