/******************************************************************************
 * channel_tag.h — Compile-time channel name NTTP (Non-Type Template Parameter)
 *
 * Replaces std::constant_wrapper<"string"> usage after P4206R0 reverted
 * string support. Provides ChannelTag<N> as a structural type usable in
 * template parameter lists (C++20).
 *
 * Usage:
 *   ChannelWriter<world::base::ChannelTag{"/world/lidar/state"}> writer;
 *
 * PRD: #402 [DistributedCore] Implement P4206R0 ChannelTag NTTP
 *****************************************************************************/
#ifndef CYBER_BASE_CHANNEL_TAG_H_
#define CYBER_BASE_CHANNEL_TAG_H_

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace world {
namespace base {

/// ChannelTag — fixed-size character array suitable as an NTTP.
///
/// Deduction guide allows `ChannelTag{"/world/lidar/state"}` to deduce N
/// automatically. The trailing NUL is included in the array but excluded
/// from view().
template <std::size_t N>
struct ChannelTag {
  char value[N]{};

  /// Implicit construction from a string literal of matching length.
  constexpr ChannelTag(const char (&s)[N]) noexcept {
    std::copy_n(s, N, value);
  }

  /// Return a string_view over the channel name (excludes NUL terminator).
  [[nodiscard]] constexpr std::string_view view() const noexcept {
    return {value, N - 1};
  }

  /// Allow implicit conversion to string_view for ergonomic use in
  /// runtime contexts (e.g. logging, map lookups).
  constexpr operator std::string_view() const noexcept { return view(); }

  /// Equality — structural types require this for NTTP deduction.
  template <std::size_t M>
  constexpr bool operator==(const ChannelTag<M>& other) const noexcept {
    if constexpr (N != M) return false;
    else {
      for (std::size_t i = 0; i < N; ++i) {
        if (value[i] != other.value[i]) return false;
      }
      return true;
    }
  }
  template <std::size_t M>
  constexpr bool operator!=(const ChannelTag<M>& other) const noexcept {
    return !(*this == other);
  }

  /// Compile-time hash — usable in constexpr map/set implementations.
  [[nodiscard]] constexpr std::size_t hash() const noexcept {
    // FNV-1a on the payload (excluding NUL).
    std::size_t h = 14695981039346656037ULL;
    for (std::size_t i = 0; i + 1 < N; ++i) {
      h ^= static_cast<std::size_t>(static_cast<unsigned char>(value[i]));
      h *= 1099511628211ULL;
    }
    return h;
  }
};

/// Deduction guide so callers write ChannelTag{"foo"} without <N>.
template <std::size_t N>
ChannelTag(const char (&)[N]) -> ChannelTag<N>;

}  // namespace base
}  // namespace world

/// std::hash specialisation for ChannelTag so it works in std::unordered_map.
namespace std {
template <std::size_t N>
struct hash<world::base::ChannelTag<N>> {
  constexpr std::size_t operator()(
      const world::base::ChannelTag<N>& tag) const noexcept {
    return tag.hash();
  }
};
}  // namespace std

#endif  // CYBER_BASE_CHANNEL_TAG_H_
