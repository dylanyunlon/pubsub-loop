/******************************************************************************
 * epoch_retire_queue.h — Deferred reclamation of retired individual states
 *
 * PRD #236: When an individual departs (status DEPARTING), its state record
 * is not freed immediately — other CRoutines may still hold raw pointers to
 * it from the current epoch. Instead, the pointer is enqueued with its retire
 * epoch. reclaim_ready() frees all entries whose retire epoch is at or below
 * the EpochManager's safe_reclaim_epoch().
 *
 * Thread safety:
 *   - retire() is called from the tick coordinator thread only (single writer)
 *   - reclaim_ready() is called from the tick coordinator thread only
 *   - No concurrent access to the queue — mutex-free by design
 *
 * Memory bound:
 *   With 1% departure rate, 100K individuals, and reclamation within 2 ticks,
 *   the queue holds at most ~2000 entries (bounded memory growth).
 *****************************************************************************/

#ifndef CYBER_COMPONENT_EPOCH_RETIRE_QUEUE_H_
#define CYBER_COMPONENT_EPOCH_RETIRE_QUEUE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "cyber/component/epoch_manager.h"

namespace world {
namespace cyber {
namespace component {

template <typename T>
class EpochRetireQueue {
 public:
  using Deleter = std::function<void(T*)>;

  /**
   * Default constructor: uses operator delete for reclamation.
   */
  EpochRetireQueue() : deleter_([](T* p) { delete p; }) {}

  /**
   * Constructor with custom deleter (e.g., return to pool).
   */
  explicit EpochRetireQueue(Deleter deleter) : deleter_(std::move(deleter)) {}

  ~EpochRetireQueue() {
    // Forcefully reclaim everything on destruction
    for (auto& entry : queue_) {
      if (entry.ptr) {
        deleter_(entry.ptr);
      }
    }
    queue_.clear();
  }

  // Non-copyable
  EpochRetireQueue(const EpochRetireQueue&) = delete;
  EpochRetireQueue& operator=(const EpochRetireQueue&) = delete;

  // Movable
  EpochRetireQueue(EpochRetireQueue&& other) noexcept
      : queue_(std::move(other.queue_)),
        deleter_(std::move(other.deleter_)),
        total_retired_(other.total_retired_),
        total_reclaimed_(other.total_reclaimed_) {
    other.total_retired_ = 0;
    other.total_reclaimed_ = 0;
  }

  EpochRetireQueue& operator=(EpochRetireQueue&& other) noexcept {
    if (this != &other) {
      // Reclaim our existing entries
      for (auto& entry : queue_) {
        if (entry.ptr) deleter_(entry.ptr);
      }
      queue_ = std::move(other.queue_);
      deleter_ = std::move(other.deleter_);
      total_retired_ = other.total_retired_;
      total_reclaimed_ = other.total_reclaimed_;
      other.total_retired_ = 0;
      other.total_reclaimed_ = 0;
    }
    return *this;
  }

  /**
   * Retire a pointer at the given epoch.
   * The pointer will be freed once safe_reclaim_epoch() advances past
   * retire_epoch.
   *
   * Also triggers a reclamation pass — safe entries are freed immediately.
   */
  void retire(T* ptr, uint64_t retire_epoch, const EpochManager& mgr) {
    queue_.push_back({ptr, retire_epoch});
    ++total_retired_;
    reclaim_ready(mgr);
  }

  /**
   * Free all entries whose retire_epoch <= mgr.safe_reclaim_epoch().
   * Returns the number of entries reclaimed in this call.
   */
  size_t reclaim_ready(const EpochManager& mgr) {
    uint64_t safe = mgr.safe_reclaim_epoch();
    size_t reclaimed = 0;

    // Partition: move reclaimable entries to the front via swap-and-pop
    size_t write = 0;
    for (size_t read = 0; read < queue_.size(); ++read) {
      if (queue_[read].retire_epoch <= safe) {
        deleter_(queue_[read].ptr);
        queue_[read].ptr = nullptr;
        ++reclaimed;
      } else {
        if (write != read) {
          queue_[write] = queue_[read];
        }
        ++write;
      }
    }
    queue_.resize(write);

    total_reclaimed_ += reclaimed;
    return reclaimed;
  }

  /**
   * Number of entries currently waiting for reclamation.
   */
  size_t pending_count() const { return queue_.size(); }

  /**
   * Lifetime statistics.
   */
  uint64_t total_retired() const { return total_retired_; }
  uint64_t total_reclaimed() const { return total_reclaimed_; }

 private:
  struct RetiredEntry {
    T* ptr;
    uint64_t retire_epoch;
  };

  std::vector<RetiredEntry> queue_;
  Deleter deleter_;
  uint64_t total_retired_ = 0;
  uint64_t total_reclaimed_ = 0;
};

}  // namespace component
}  // namespace cyber
}  // namespace world

#endif  // CYBER_COMPONENT_EPOCH_RETIRE_QUEUE_H_
