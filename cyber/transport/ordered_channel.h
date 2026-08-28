/******************************************************************************
 * ordered_channel.h — Tick-barrier ordered state delivery
 *
 * PRD #222: When multiple individuals publish state through the same channel,
 * subscribers receive all states for tick N before any state from tick N+1.
 * Within a tick, states are sorted by IndividualId for deterministic replay.
 *
 * Protocol:
 *   1. Each publisher calls write(state, tick) then commit_tick(tick)
 *   2. TickBarrier collects commits; once all known publishers commit tick N,
 *      it assembles a TickSnapshot sorted by IndividualId and delivers it
 *   3. If a publisher dies mid-tick, timeout fires and delivers PARTIAL_TICK
 *
 * Works across Shm, RTPS, and IntraProcess transport backends.
 *****************************************************************************/

#ifndef CYBER_TRANSPORT_ORDERED_CHANNEL_H_
#define CYBER_TRANSPORT_ORDERED_CHANNEL_H_

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cyber/time/tick_clock.h"
#include "cyber/transport/individual_state.h"

namespace world {
namespace cyber {
namespace transport {

using WorldTick = world::cyber::time::WorldTick;

// ─── TickSnapshot ────────────────────────────────────────────────────────────

enum class TickDeliveryFlag : uint8_t {
  kComplete    = 0,    // All known publishers committed
  kPartialTick = 1,    // One or more publishers timed out
};

template <typename T>
struct TickSnapshot {
  WorldTick tick = 0;
  TickDeliveryFlag flag = TickDeliveryFlag::kComplete;
  std::vector<std::pair<IndividualId, T>> states;

  const T* find(IndividualId id) const {
    // states are sorted by IndividualId — binary search
    auto it = std::lower_bound(
        states.begin(), states.end(), id,
        [](const auto& p, IndividualId target) { return p.first < target; });
    if (it != states.end() && it->first == id) return &it->second;
    return nullptr;
  }

  size_t size() const { return states.size(); }
  bool empty() const { return states.empty(); }
  bool is_partial() const { return flag == TickDeliveryFlag::kPartialTick; }

  auto begin() const { return states.begin(); }
  auto end() const { return states.end(); }
};

// ─── TickBarrier (internal) ──────────────────────────────────────────────────
//
// Aggregates writes and commits from multiple publishers.
// Thread-safe: multiple writers call concurrently, one reader drains.

template <typename T>
class TickBarrier {
 public:
  struct Config {
    uint32_t deadline_ms = 100;    // Timeout for partial tick delivery
    uint32_t max_buffered_ticks = 8;
  };

  explicit TickBarrier(Config config = {}) : config_(config) {}

  // Called by writers
  void write(IndividualId publisher, const T& state, WorldTick tick);
  void commit(IndividualId publisher, WorldTick tick);

  // Called when a publisher joins/leaves
  void register_publisher(IndividualId publisher);
  void unregister_publisher(IndividualId publisher);
  size_t num_publishers() const;

  // Called by reader — blocks until tick is ready or deadline
  TickSnapshot<T> wait_tick(WorldTick tick);
  std::optional<TickSnapshot<T>> try_wait_tick(WorldTick tick,
                                                std::chrono::milliseconds timeout);

  // Housekeeping: discard ticks older than the given watermark
  void gc(WorldTick watermark);

 private:
  struct TickBucket {
    std::map<IndividualId, T> states;   // Sorted by IndividualId implicitly
    std::set<IndividualId> committed;
    bool delivered = false;
  };

  TickSnapshot<T> assemble(WorldTick tick, TickBucket& bucket,
                           TickDeliveryFlag flag);
  bool tick_ready(WorldTick tick) const;

  Config config_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::set<IndividualId> publishers_;
  std::map<WorldTick, TickBucket> buckets_;
};

// ─── OrderedChannel ──────────────────────────────────────────────────────────

template <typename T>
class OrderedChannel {
 public:
  class Writer;
  class Reader;

  struct Config {
    std::string channel_name;
    uint32_t deadline_ms = 100;
  };

  static std::shared_ptr<OrderedChannel> create(const Config& config);

  std::shared_ptr<Writer> create_writer(IndividualId publisher_id);
  std::shared_ptr<Reader> create_reader();

  // Access to underlying barrier (for monitoring/testing)
  TickBarrier<T>& barrier() { return barrier_; }
  const TickBarrier<T>& barrier() const { return barrier_; }

  const std::string& channel_name() const { return config_.channel_name; }

 private:
  explicit OrderedChannel(const Config& config);

  Config config_;
  TickBarrier<T> barrier_;
};

// ─── Writer ──────────────────────────────────────────────────────────────────

template <typename T>
class OrderedChannel<T>::Writer {
 public:
  Writer(std::shared_ptr<OrderedChannel<T>> channel, IndividualId id);
  ~Writer();

  void write(const T& state, WorldTick tick);
  void commit_tick(WorldTick tick);

  IndividualId id() const { return id_; }

 private:
  std::shared_ptr<OrderedChannel<T>> channel_;
  IndividualId id_;
  WorldTick last_committed_tick_ = 0;
};

// ─── Reader ──────────────────────────────────────────────────────────────────

template <typename T>
class OrderedChannel<T>::Reader {
 public:
  explicit Reader(std::shared_ptr<OrderedChannel<T>> channel);

  // Blocking: waits for next tick snapshot
  TickSnapshot<T> read_tick();

  // Non-blocking: returns nullopt if not ready
  std::optional<TickSnapshot<T>> try_read_tick();

  WorldTick next_expected_tick() const { return next_tick_; }

 private:
  std::shared_ptr<OrderedChannel<T>> channel_;
  WorldTick next_tick_ = 1;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Template implementation
// ═══════════════════════════════════════════════════════════════════════════════

// ─── TickBarrier impl ────────────────────────────────────────────────────────

template <typename T>
void TickBarrier<T>::write(IndividualId publisher, const T& state,
                           WorldTick tick) {
  std::lock_guard<std::mutex> lk(mu_);
  buckets_[tick].states[publisher] = state;
}

template <typename T>
void TickBarrier<T>::commit(IndividualId publisher, WorldTick tick) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    buckets_[tick].committed.insert(publisher);
  }
  cv_.notify_all();
}

template <typename T>
void TickBarrier<T>::register_publisher(IndividualId publisher) {
  std::lock_guard<std::mutex> lk(mu_);
  publishers_.insert(publisher);
}

template <typename T>
void TickBarrier<T>::unregister_publisher(IndividualId publisher) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    publishers_.erase(publisher);
  }
  cv_.notify_all();  // May unblock a waiting reader
}

template <typename T>
size_t TickBarrier<T>::num_publishers() const {
  std::lock_guard<std::mutex> lk(mu_);
  return publishers_.size();
}

template <typename T>
bool TickBarrier<T>::tick_ready(WorldTick tick) const {
  // Caller holds mu_
  auto it = buckets_.find(tick);
  if (it == buckets_.end()) return false;
  if (publishers_.empty()) return false;
  return it->second.committed.size() >= publishers_.size();
}

template <typename T>
TickSnapshot<T> TickBarrier<T>::wait_tick(WorldTick tick) {
  std::unique_lock<std::mutex> lk(mu_);
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(config_.deadline_ms);

  bool ready = cv_.wait_until(lk, deadline, [&] { return tick_ready(tick); });

  TickDeliveryFlag flag = ready ? TickDeliveryFlag::kComplete
                                : TickDeliveryFlag::kPartialTick;
  return assemble(tick, buckets_[tick], flag);
}

template <typename T>
std::optional<TickSnapshot<T>> TickBarrier<T>::try_wait_tick(
    WorldTick tick, std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lk(mu_);
  auto deadline = std::chrono::steady_clock::now() + timeout;

  bool ready = cv_.wait_until(lk, deadline, [&] { return tick_ready(tick); });

  if (!ready) {
    // Check if we have any data at all for this tick
    auto it = buckets_.find(tick);
    if (it == buckets_.end() || it->second.states.empty()) {
      return std::nullopt;
    }
    return assemble(tick, it->second, TickDeliveryFlag::kPartialTick);
  }
  return assemble(tick, buckets_[tick], TickDeliveryFlag::kComplete);
}

template <typename T>
TickSnapshot<T> TickBarrier<T>::assemble(WorldTick tick, TickBucket& bucket,
                                         TickDeliveryFlag flag) {
  // Caller holds mu_
  TickSnapshot<T> snap;
  snap.tick = tick;
  snap.flag = flag;
  snap.states.reserve(bucket.states.size());

  // std::map iteration is already sorted by IndividualId (operator<)
  for (auto& [id, state] : bucket.states) {
    snap.states.emplace_back(id, std::move(state));
  }

  bucket.delivered = true;
  return snap;
}

template <typename T>
void TickBarrier<T>::gc(WorldTick watermark) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = buckets_.begin();
  while (it != buckets_.end() && it->first < watermark) {
    it = buckets_.erase(it);
  }
}

// ─── OrderedChannel impl ────────────────────────────────────────────────────

template <typename T>
OrderedChannel<T>::OrderedChannel(const Config& config)
    : config_(config),
      barrier_(typename TickBarrier<T>::Config{config.deadline_ms, 8}) {}

template <typename T>
std::shared_ptr<OrderedChannel<T>> OrderedChannel<T>::create(
    const Config& config) {
  // Can't use make_shared with private ctor
  return std::shared_ptr<OrderedChannel<T>>(new OrderedChannel<T>(config));
}

template <typename T>
std::shared_ptr<typename OrderedChannel<T>::Writer>
OrderedChannel<T>::create_writer(IndividualId publisher_id) {
  barrier_.register_publisher(publisher_id);
  // shared_from_this not available, pass raw shared_ptr from create()
  // Caller must ensure OrderedChannel outlives Writer
  return std::make_shared<Writer>(
      std::shared_ptr<OrderedChannel<T>>(this, [](OrderedChannel<T>*) {}),
      publisher_id);
}

template <typename T>
std::shared_ptr<typename OrderedChannel<T>::Reader>
OrderedChannel<T>::create_reader() {
  return std::make_shared<Reader>(
      std::shared_ptr<OrderedChannel<T>>(this, [](OrderedChannel<T>*) {}));
}

// ─── Writer impl ─────────────────────────────────────────────────────────────

template <typename T>
OrderedChannel<T>::Writer::Writer(std::shared_ptr<OrderedChannel<T>> channel,
                                  IndividualId id)
    : channel_(std::move(channel)), id_(id) {}

template <typename T>
OrderedChannel<T>::Writer::~Writer() {
  if (channel_) {
    channel_->barrier_.unregister_publisher(id_);
  }
}

template <typename T>
void OrderedChannel<T>::Writer::write(const T& state, WorldTick tick) {
  channel_->barrier_.write(id_, state, tick);
}

template <typename T>
void OrderedChannel<T>::Writer::commit_tick(WorldTick tick) {
  channel_->barrier_.commit(id_, tick);
  last_committed_tick_ = tick;
}

// ─── Reader impl ─────────────────────────────────────────────────────────────

template <typename T>
OrderedChannel<T>::Reader::Reader(std::shared_ptr<OrderedChannel<T>> channel)
    : channel_(std::move(channel)) {}

template <typename T>
TickSnapshot<T> OrderedChannel<T>::Reader::read_tick() {
  auto snap = channel_->barrier_.wait_tick(next_tick_);
  next_tick_ = snap.tick + 1;
  // GC old ticks — keep last 4 for late readers
  if (snap.tick > 4) {
    channel_->barrier_.gc(snap.tick - 4);
  }
  return snap;
}

template <typename T>
std::optional<TickSnapshot<T>> OrderedChannel<T>::Reader::try_read_tick() {
  auto snap = channel_->barrier_.try_wait_tick(
      next_tick_, std::chrono::milliseconds(0));
  if (snap.has_value()) {
    next_tick_ = snap->tick + 1;
  }
  return snap;
}

}  // namespace transport
}  // namespace cyber
}  // namespace world

#endif  // CYBER_TRANSPORT_ORDERED_CHANNEL_H_
