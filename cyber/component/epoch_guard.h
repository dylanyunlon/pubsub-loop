/******************************************************************************
 * epoch_guard.h — RAII epoch pin/unpin for CRoutine individual processing
 *
 * PRD #236: A coroutine that processes IndividualState records constructs an
 * EpochGuard at the start of its batch. The guard pins the current epoch,
 * ensuring no record retired after that epoch is reclaimed while the guard
 * is alive. On destruction (scope exit or coroutine yield), the pin is
 * released so reclamation can advance.
 *
 * Usage in the hot path:
 *   void process_batch(EpochManager& mgr, int observer_id) {
 *       EpochGuard guard(mgr, observer_id);
 *       // All reads of IndividualState* are safe until guard dtor
 *       for (auto* state : batch) { process(*state); }
 *   }  // ~EpochGuard unpins — reclamation may now proceed
 *
 * Non-copyable, non-movable (tied to a specific scope).
 *****************************************************************************/

#ifndef CYBER_COMPONENT_EPOCH_GUARD_H_
#define CYBER_COMPONENT_EPOCH_GUARD_H_

#include <cstdint>

#include "cyber/component/epoch_manager.h"

namespace world {
namespace cyber {
namespace component {

class EpochGuard {
 public:
  /**
   * Pin the current epoch for the given observer.
   */
  EpochGuard(EpochManager& mgr, int observer_id) noexcept
      : mgr_(mgr), observer_id_(observer_id) {
    pinned_epoch_ = mgr_.pin_epoch(observer_id_);
  }

  /**
   * Unpin on destruction. Safe reclamation may advance past this epoch.
   */
  ~EpochGuard() noexcept {
    mgr_.unpin_epoch(observer_id_);
  }

  // Non-copyable, non-movable
  EpochGuard(const EpochGuard&) = delete;
  EpochGuard& operator=(const EpochGuard&) = delete;
  EpochGuard(EpochGuard&&) = delete;
  EpochGuard& operator=(EpochGuard&&) = delete;

  /**
   * The epoch this guard pinned at construction time.
   */
  uint64_t epoch() const noexcept { return pinned_epoch_; }

 private:
  EpochManager& mgr_;
  int observer_id_;
  uint64_t pinned_epoch_;
};

}  // namespace component
}  // namespace cyber
}  // namespace world

#endif  // CYBER_COMPONENT_EPOCH_GUARD_H_
