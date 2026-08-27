/******************************************************************************
 * sort_croutine.h — Sort CRoutine task for individual state arrays
 *
 * PRD #56: Uses `if constexpr` instead of `std::bool_constant` tag dispatch.
 * Only the selected branch is instantiated — the unused comparator never
 * appears in the compiled object file.
 *
 * Sources:
 *   - Apollo CyberRT: croutine/croutine.h (CRoutine lifecycle)
 *   - NVIDIA CUB: DeviceRadixSort (ascending/descending split)
 *   - pubsub-loop: scheduler/scheduler.h (processor/task integration)
 *
 * Namespace: world::cyber::scheduler (migrated from apollo::cyber)
 *****************************************************************************/

#ifndef CYBER_SCHEDULER_SORT_CROUTINE_H_
#define CYBER_SCHEDULER_SORT_CROUTINE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace world {
namespace cyber {
namespace scheduler {

/**
 * SortCRoutine — sort a contiguous range of individual states.
 *
 * @tparam StateT  Element type (e.g. IndividualState, float, …)
 * @tparam Desc    true → descending (std::greater<>),
 *                 false → ascending  (std::less<>)
 *
 * if constexpr guarantees dead-branch elimination at template instantiation
 * time: SortCRoutine<float, false> will contain zero references to
 * std::greater, and vice versa.
 *
 * Usage:
 *   SortCRoutine<float, false> asc;
 *   asc.execute({states.data(), states.size()});
 *
 *   SortCRoutine<IndividualState, true> desc;
 *   desc.execute({arr, n});
 */
template <typename StateT, bool Desc>
class SortCRoutine {
 public:
  /**
   * Lightweight span — avoids dependency on <span> (C++20) while this
   * codebase targets C++17.  Zero-overhead wrapper around pointer + size.
   */
  struct Span {
    StateT* data;
    std::size_t size;

    StateT* begin() noexcept { return data; }
    StateT* end()   noexcept { return data + size; }
  };

  SortCRoutine() = default;

  /**
   * Sort the full span in-place.
   *
   * Empty and single-element spans are safe no-ops.
   */
  void execute(Span states) noexcept {
    if (states.size <= 1) return;

    if constexpr (Desc) {
      // Descending: std::greater<>  — only instantiated when Desc == true
      std::stable_sort(states.begin(), states.end(), std::greater<StateT>{});
    } else {
      // Ascending: std::less<>      — only instantiated when Desc == false
      std::stable_sort(states.begin(), states.end(), std::less<StateT>{});
    }
  }

  /**
   * Sort a sub-range [start, end) within a larger buffer.
   *
   * Bounds are clamped silently — no UB on out-of-range indices.
   */
  void execute_range(StateT* data, std::size_t total,
                     std::size_t start, std::size_t end) noexcept {
    if (!data || start >= end || start >= total) return;
    if (end > total) end = total;

    if constexpr (Desc) {
      std::stable_sort(data + start, data + end, std::greater<StateT>{});
    } else {
      std::stable_sort(data + start, data + end, std::less<StateT>{});
    }
  }

  /**
   * Sort with a custom projection (key extractor).
   *
   * @tparam Proj  Callable: StateT → comparable value
   *
   * Example:
   *   sort.execute_with_key(span, [](const IndividualState& s) {
   *     return s.position.x;
   *   });
   */
  template <typename Proj>
  void execute_with_key(Span states, Proj proj) noexcept {
    if (states.size <= 1) return;

    if constexpr (Desc) {
      std::stable_sort(states.begin(), states.end(),
                       [&proj](const StateT& a, const StateT& b) {
                         return proj(a) > proj(b);
                       });
    } else {
      std::stable_sort(states.begin(), states.end(),
                       [&proj](const StateT& a, const StateT& b) {
                         return proj(a) < proj(b);
                       });
    }
  }

  /**
   * Compile-time query: is this a descending sorter?
   */
  static constexpr bool is_descending = Desc;
};

// Convenience aliases
template <typename StateT>
using AscendingSortCRoutine = SortCRoutine<StateT, false>;

template <typename StateT>
using DescendingSortCRoutine = SortCRoutine<StateT, true>;

}  // namespace scheduler
}  // namespace cyber
}  // namespace world

#endif  // CYBER_SCHEDULER_SORT_CROUTINE_H_
