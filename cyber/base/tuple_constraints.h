/******************************************************************************
 * tuple_constraints.h — Compile-time safety checks for tuple-based messages.
 *
 * Ensures every element of a tuple satisfies is_message_safe_v (trivially
 * copyable + standard layout) before it can be used in ChannelBuffer or
 * ZeroCopyRegion.  Uses C++20 fold expressions for O(N) instantiation
 * depth instead of the former recursive-specialisation O(N²) approach.
 *
 * PRD: #403 [DistributedCore] Reformulate tuple constraints
 *****************************************************************************/
#ifndef CYBER_BASE_TUPLE_CONSTRAINTS_H_
#define CYBER_BASE_TUPLE_CONSTRAINTS_H_

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace world {
namespace base {

// ---------------------------------------------------------------------------
// is_message_safe — a type is "message safe" when it can be memcpy'd across
// a pub/sub transport boundary (SHM segment, zero-copy region).
// ---------------------------------------------------------------------------
template <typename T>
struct is_message_safe
    : std::conjunction<std::is_trivially_copyable<T>,
                       std::is_standard_layout<T>> {};

template <typename T>
inline constexpr bool is_message_safe_v = is_message_safe<T>::value;

// ---------------------------------------------------------------------------
// TupleElementsSafe — concept that checks every element of a tuple-like type.
//
// Implementation uses a fold expression over an index sequence, so the
// compiler instantiates exactly N trait checks for a tuple of N elements
// (was O(N²) with the old recursive TupleConstraintImpl chain).
// ---------------------------------------------------------------------------
namespace detail {

template <typename Tuple, std::size_t... Is>
consteval bool all_elements_safe(std::index_sequence<Is...>) {
  return (is_message_safe_v<std::tuple_element_t<Is, Tuple>> && ...);
}

}  // namespace detail

/// True iff every element of @p Tuple is message-safe.
template <typename Tuple>
inline constexpr bool tuple_elements_safe_v =
    detail::all_elements_safe<Tuple>(
        std::make_index_sequence<std::tuple_size_v<Tuple>>{});

/// Concept form — use in template constraints / requires clauses.
template <typename Tuple>
concept TupleElementsSafe = tuple_elements_safe_v<Tuple>;

// ---------------------------------------------------------------------------
// Static diagnostic — when a tuple fails the constraint, this helper
// produces a readable compile error listing the offending index.
// ---------------------------------------------------------------------------
namespace detail {

template <typename Tuple, std::size_t I>
struct diagnose_unsafe_element {
  static_assert(is_message_safe_v<std::tuple_element_t<I, Tuple>>,
                "Tuple element at this index is not message-safe "
                "(must be trivially copyable + standard layout).");
};

template <typename Tuple, std::size_t... Is>
constexpr void diagnose_all(std::index_sequence<Is...>) {
  (void)std::initializer_list<int>{
      (diagnose_unsafe_element<Tuple, Is>{}, 0)...};
}

}  // namespace detail

/// Call this in a static_assert context to get per-element diagnostics.
template <typename Tuple>
constexpr void diagnose_tuple_safety() {
  detail::diagnose_all<Tuple>(
      std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

// ---------------------------------------------------------------------------
// Convenience: assert_tuple_safe<T> — drop-in guard for ChannelBuffer /
// ZeroCopyRegion declarations.
// ---------------------------------------------------------------------------
template <typename Tuple>
struct assert_tuple_safe {
  static_assert(tuple_elements_safe_v<Tuple>,
                "All elements of the tuple must be message-safe "
                "(trivially copyable + standard layout) for zero-copy "
                "transport.");
};

}  // namespace base
}  // namespace world

#endif  // CYBER_BASE_TUPLE_CONSTRAINTS_H_
