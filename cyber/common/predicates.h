/******************************************************************************
 * predicates.h — Unified always_true / always_false state-filter predicates.
 *
 * Replaces scattered lambda definitions with constexpr functor types that
 * can be used as NTTP and are guaranteed to be fully inlined at -O2.
 *
 * PRD: #133 Unify always_true/always_false under common::
 *****************************************************************************/
#ifndef CYBER_COMMON_PREDICATES_H_
#define CYBER_COMMON_PREDICATES_H_

#include <type_traits>

namespace world {
namespace common {

/// Always-true predicate: constexpr, zero-overhead in tick-gate dispatch.
/// When used as template parameter, the branch is eliminated entirely.
struct always_true_t {
  template <typename T>
  [[nodiscard]] constexpr bool operator()(const T& /*unused*/) const noexcept {
    return true;
  }
};

/// Always-false predicate: suppresses publish; zero overhead.
struct always_false_t {
  template <typename T>
  [[nodiscard]] constexpr bool operator()(const T& /*unused*/) const noexcept {
    return false;
  }
};

/// Inline constexpr instances for convenience.
inline constexpr always_true_t  always_true{};
inline constexpr always_false_t always_false{};

// ---------------------------------------------------------------------------
// Type traits for predicate detection
// ---------------------------------------------------------------------------

/// True if P is an always-true predicate (compile-time gate elimination).
template <typename P>
struct is_always_true : std::is_same<std::decay_t<P>, always_true_t> {};

template <typename P>
inline constexpr bool is_always_true_v = is_always_true<P>::value;

/// True if P is an always-false predicate (compile-time gate elimination).
template <typename P>
struct is_always_false : std::is_same<std::decay_t<P>, always_false_t> {};

template <typename P>
inline constexpr bool is_always_false_v = is_always_false<P>::value;

/// True if P is a trivial predicate (always_true or always_false) whose
/// result is known at compile time — enables full branch elimination.
template <typename P>
inline constexpr bool is_trivial_predicate_v =
    is_always_true_v<P> || is_always_false_v<P>;

}  // namespace common
}  // namespace world

#endif  // CYBER_COMMON_PREDICATES_H_
