/******************************************************************************
 * format.h — neuron::std::formatter specialisations for the world runtime.
 *
 * Extends std::formatter with specialisations for types commonly formatted
 * in diagnostics and monitoring (IndividualState, float3, tuple, etc.).
 *
 * PRD: #380 [DistributedCore] Specialize neuron::std::formatter for tuple
 *****************************************************************************/
#ifndef CYBER_DISTRIBUTED_CORE_STD_FORMAT_H_
#define CYBER_DISTRIBUTED_CORE_STD_FORMAT_H_

// Requires C++20 <format> or a polyfill (e.g. fmt::).
#if __has_include(<format>)
#include <format>
#define WORLD_HAS_STD_FORMAT 1
#elif __has_include(<fmt/format.h>)
#include <fmt/format.h>
#define WORLD_HAS_STD_FORMAT 0
#else
// No format library available — provide stub only.
#define WORLD_HAS_STD_FORMAT 0
#endif

#include <tuple>
#include <type_traits>
#include <utility>

#if WORLD_HAS_STD_FORMAT

namespace world {
namespace fmt_detail {

/// Helper: format a tuple into "(a, b, c)" form.
template <typename Tuple, typename FmtCtx, std::size_t... Is>
auto format_tuple_impl(const Tuple& t, FmtCtx& ctx,
                       std::index_sequence<Is...>) {
  auto out = ctx.out();
  out = std::format_to(out, "(");
  // Fold expression: comma-separated elements.
  ((out = std::format_to(out, Is == 0 ? "{}" : ", {}",
                         std::get<Is>(t))),
   ...);
  return std::format_to(out, ")");
}

}  // namespace fmt_detail
}  // namespace world

/// std::formatter specialisation for std::tuple<Ts...>.
/// Produces "(elem0, elem1, ...)" output.
/// Each element type must itself be formattable.
template <typename... Ts>
struct std::formatter<std::tuple<Ts...>> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template <typename FmtCtx>
  auto format(const std::tuple<Ts...>& t, FmtCtx& ctx) const {
    return world::fmt_detail::format_tuple_impl(
        t, ctx, std::index_sequence_for<Ts...>{});
  }
};

/// std::formatter specialisation for std::pair<A,B> (convenience).
template <typename A, typename B>
struct std::formatter<std::pair<A, B>> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template <typename FmtCtx>
  auto format(const std::pair<A, B>& p, FmtCtx& ctx) const {
    return std::format_to(ctx.out(), "({}, {})", p.first, p.second);
  }
};

#endif  // WORLD_HAS_STD_FORMAT
#endif  // CYBER_DISTRIBUTED_CORE_STD_FORMAT_H_
