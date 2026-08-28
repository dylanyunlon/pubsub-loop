/******************************************************************************
 * saturating_cast.h — Branchless saturating integer cast.
 *
 * Converts between integral types, clamping at the target's min/max instead
 * of wrapping.  The implementation uses std::clamp / CMOV to avoid branches
 * on the hot path (individual state serialisation, tick counters).
 *
 * PRD: #416 [DistributedCore] Optimize neuron::std::saturating_cast
 *****************************************************************************/
#ifndef CYBER_DISTRIBUTED_CORE_STD_SATURATING_CAST_H_
#define CYBER_DISTRIBUTED_CORE_STD_SATURATING_CAST_H_

#include <algorithm>
#include <limits>
#include <type_traits>

namespace world {
namespace numeric {

namespace detail {

// --- Generic path (signed From): two-sided clamp → 2 × CMOV. ---------------
template <typename To, typename From,
          bool NeedLowerClamp = std::is_signed_v<From>>
struct saturating_cast_impl {
  [[nodiscard]] static constexpr To cast(From value) noexcept {
    using Limits  = std::numeric_limits<To>;
    using Common  = std::common_type_t<From, To>;

    constexpr Common lo = static_cast<Common>(Limits::min());
    constexpr Common hi = static_cast<Common>(Limits::max());

    return static_cast<To>(
        std::clamp(static_cast<Common>(value), lo, hi));
  }
};

// --- Unsigned From: only upper clamp needed → 1 × CMOV. --------------------
template <typename To, typename From>
struct saturating_cast_impl<To, From, /*NeedLowerClamp=*/false> {
  [[nodiscard]] static constexpr To cast(From value) noexcept {
    using Limits  = std::numeric_limits<To>;
    using Common  = std::common_type_t<From, To>;

    constexpr Common hi = static_cast<Common>(Limits::max());
    return static_cast<To>(
        static_cast<Common>(value) > hi
            ? hi
            : static_cast<Common>(value));
  }
};

// --- Same-type identity: no work at all. ------------------------------------
template <typename T>
struct saturating_cast_impl<T, T, true> {
  [[nodiscard]] static constexpr T cast(T value) noexcept { return value; }
};
template <typename T>
struct saturating_cast_impl<T, T, false> {
  [[nodiscard]] static constexpr T cast(T value) noexcept { return value; }
};

// --- Widening cast (To strictly contains From): static_cast suffices. -------
template <typename To, typename From>
inline constexpr bool is_widening_v =
    (sizeof(To) > sizeof(From)) &&
    (std::is_signed_v<To> || !std::is_signed_v<From>);

}  // namespace detail

/// Saturating cast from @p From to @p To.
/// Clamps @p value at numeric_limits<To>::min() / max() instead of wrapping.
/// Compiles to branchless CMOV sequences on x86/AArch64.
template <typename To, typename From>
[[nodiscard]] constexpr To saturating_cast(From value) noexcept {
  static_assert(std::is_integral_v<To> && std::is_integral_v<From>,
                "saturating_cast supports integral types only");

  // Fast path: widening conversions can never overflow.
  if constexpr (detail::is_widening_v<To, From>) {
    return static_cast<To>(value);
  } else {
    return detail::saturating_cast_impl<To, From>::cast(value);
  }
}

}  // namespace numeric
}  // namespace world

#endif  // CYBER_DISTRIBUTED_CORE_STD_SATURATING_CAST_H_
