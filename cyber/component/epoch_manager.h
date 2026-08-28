/******************************************************************************
 * epoch_manager.h — Lock-free epoch-based reclamation for IndividualState
 *
 * PRD #236: Replaces shared_ptr refcount in DistributedInteractionLoop.
 *
 * Protocol:
 *   1. Tick coordinator calls advance_epoch() at tick start
 *   2. Each CRoutine pins the current epoch via EpochGuard before reading
 *   3. When an individual departs, its state is retired at the current epoch
 *   4. safe_reclaim_epoch() scans all observer slots; any record retired
 *      at epoch <= safe can be freed without use-after-free
 *
 * Observer table: 256 cache-line-aligned atomic slots.
 * INVALID_EPOCH means "not pinned" (slot available for reclamation scan).
 *
 * Memory model: acquire/release on observer_epochs_ slots; acq_rel on
 * global_epoch_ advancement. safe_reclaim_epoch() uses relaxed loads
 * (conservative: may delay reclamation by 1 tick, never premature).
 *****************************************************************************/

#ifndef CYBER_COMPONENT_EPOCH_MANAGER_H_
#define CYBER_COMPONENT_EPOCH_MANAGER_H_

#include <atomic>
#include <cstdint>
#include <limits>

namespace world {
namespace cyber {
namespace component {

class EpochManager {
 public:
  static constexpr uint64_t INVALID_EPOCH = std::numeric_limits<uint64_t>::max();
  static constexpr int MAX_OBSERVERS = 256;

  EpochManager() {
    for (int i = 0; i < MAX_OBSERVERS; ++i) {
      observer_epochs_[i].epoch.store(INVALID_EPOCH, std::memory_order_relaxed);
      observer_active_[i].store(false, std::memory_order_relaxed);
    }
  }

  /**
   * Register an observer (coroutine). Returns observer_id in [0, MAX_OBSERVERS).
   * Returns -1 if all slots are taken.
   */
  int register_observer() noexcept {
    for (int i = 0; i < MAX_OBSERVERS; ++i) {
      bool expected = false;
      if (observer_active_[i].compare_exchange_strong(
              expected, true, std::memory_order_acq_rel)) {
        observer_epochs_[i].epoch.store(INVALID_EPOCH,
                                        std::memory_order_release);
        return i;
      }
    }
    return -1;  // All slots occupied
  }

  /**
   * Unregister an observer. The slot becomes available for future registration.
   */
  void unregister_observer(int observer_id) noexcept {
    if (observer_id < 0 || observer_id >= MAX_OBSERVERS) return;
    observer_epochs_[observer_id].epoch.store(INVALID_EPOCH,
                                              std::memory_order_release);
    observer_active_[observer_id].store(false, std::memory_order_release);
  }

  /**
   * Pin the current epoch for this observer.
   * While pinned, no record retired at this epoch or later will be reclaimed.
   * Returns the pinned epoch value.
   */
  uint64_t pin_epoch(int observer_id) noexcept {
    uint64_t e = global_epoch_.load(std::memory_order_acquire);
    observer_epochs_[observer_id].epoch.store(e, std::memory_order_release);
    return e;
  }

  /**
   * Unpin this observer's epoch. Safe reclamation can advance past it.
   */
  void unpin_epoch(int observer_id) noexcept {
    observer_epochs_[observer_id].epoch.store(INVALID_EPOCH,
                                              std::memory_order_release);
  }

  /**
   * Advance the global epoch. Called once at each tick boundary.
   * Returns the new epoch value.
   */
  uint64_t advance_epoch() noexcept {
    return global_epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
  }

  /**
   * Compute the safe reclamation epoch.
   * Any retired record with retire_epoch <= safe_reclaim_epoch() can be freed.
   *
   * Algorithm: min(all pinned observer epochs) - 1.
   * If no observer is pinned, returns current epoch (all records reclaimable).
   */
  uint64_t safe_reclaim_epoch() const noexcept {
    uint64_t min_epoch = global_epoch_.load(std::memory_order_relaxed);
    bool any_pinned = false;

    for (int i = 0; i < MAX_OBSERVERS; ++i) {
      if (!observer_active_[i].load(std::memory_order_relaxed)) continue;
      uint64_t e = observer_epochs_[i].epoch.load(std::memory_order_relaxed);
      if (e != INVALID_EPOCH) {
        any_pinned = true;
        if (e < min_epoch) {
          min_epoch = e;
        }
      }
    }

    if (!any_pinned) {
      // No observer is pinned — everything up to current epoch is safe
      return global_epoch_.load(std::memory_order_relaxed);
    }

    // Safe to reclaim everything strictly before the oldest pinned epoch
    return (min_epoch > 0) ? min_epoch - 1 : 0;
  }

  /**
   * Current global epoch (non-authoritative read).
   */
  uint64_t current_epoch() const noexcept {
    return global_epoch_.load(std::memory_order_acquire);
  }

  /**
   * Number of currently registered observers (for diagnostics).
   */
  int num_active_observers() const noexcept {
    int count = 0;
    for (int i = 0; i < MAX_OBSERVERS; ++i) {
      if (observer_active_[i].load(std::memory_order_relaxed)) ++count;
    }
    return count;
  }

 private:
  // ── Global epoch counter ──
  std::atomic<uint64_t> global_epoch_{0};

  // ── Per-observer epoch slots ──
  // Each slot is cache-line aligned to prevent false sharing.
  struct alignas(64) ObserverSlot {
    std::atomic<uint64_t> epoch{INVALID_EPOCH};
  };

  ObserverSlot observer_epochs_[MAX_OBSERVERS];
  std::atomic<bool> observer_active_[MAX_OBSERVERS];
};

}  // namespace component
}  // namespace cyber
}  // namespace world

#endif  // CYBER_COMPONENT_EPOCH_MANAGER_H_
