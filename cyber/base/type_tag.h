/******************************************************************************
 * type_tag.h — Scalar type tags for the pub/sub wire serialization layer
 *
 * PRD #233: Extended TypeTag enum with F16 and BF16 support.
 *
 * bfloat16_t is a portable uint16_t wrapper that prevents accidental
 * arithmetic on raw bits. It is standard-layout, trivially copyable,
 * and satisfies the C-ABI wire-transport requirements for IndividualState.
 *****************************************************************************/

#ifndef CYBER_BASE_TYPE_TAG_H_
#define CYBER_BASE_TYPE_TAG_H_

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace world {
namespace cyber {
namespace base {

// ─── TypeTag ────────────────────────────────────────────────────────────────

enum class TypeTag : uint8_t {
  kF32  = 0x01,  // float   — V1
  kF64  = 0x02,  // double  — V1
  kF16  = 0x03,  // _Float16 / __fp16 — V2
  kBF16 = 0x04,  // bfloat16_t        — V2
};

// ─── bfloat16_t ─────────────────────────────────────────────────────────────
//
// Brain floating-point 16-bit: 1 sign + 8 exponent + 7 mantissa.
// Same exponent range as float32 but with reduced mantissa precision.
// Used by ML inference pipelines (GPU BF16 output → wire → GPU BF16 input).
//
// This is a storage type, not an arithmetic type. All computation goes
// through from_float()/to_float() explicit conversions.

struct bfloat16_t {
  uint16_t bits;

  // ── Conversions ──

  /// Truncate float to bfloat16 (round-to-nearest-even not applied;
  /// truncation matches GPU hardware behavior for BF16 stores).
  static bfloat16_t from_float(float f) noexcept {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    // Round-to-nearest-even: add rounding bias
    // If the truncated bits are > 0.5 ULP, or exactly 0.5 ULP with odd
    // mantissa, round up. This matches __nv_bfloat16 behavior.
    uint32_t rounding_bias = (u >> 16) & 1;  // LSB of result
    u += 0x7FFF + rounding_bias;
    return bfloat16_t{static_cast<uint16_t>(u >> 16)};
  }

  /// Exact expansion to float (zero-extends mantissa).
  float to_float() const noexcept {
    uint32_t u = static_cast<uint32_t>(bits) << 16;
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
  }

  // ── Comparison ──

  constexpr bool operator==(const bfloat16_t& o) const {
    return bits == o.bits;
  }
  constexpr bool operator!=(const bfloat16_t& o) const {
    return bits != o.bits;
  }
};

// Layout requirements for zero-copy wire transport
static_assert(sizeof(bfloat16_t) == 2, "bfloat16_t must be 2 bytes");
static_assert(alignof(bfloat16_t) == 2, "bfloat16_t must be 2-byte aligned");
static_assert(std::is_standard_layout_v<bfloat16_t>,
              "bfloat16_t must be standard-layout for C-ABI transport");
static_assert(std::is_trivially_copyable_v<bfloat16_t>,
              "bfloat16_t must be trivially copyable for zero-copy SHM");

// ─── TypeTag traits ─────────────────────────────────────────────────────────

template <typename T>
struct TypeTagOf;

template <>
struct TypeTagOf<float> {
  static constexpr TypeTag value = TypeTag::kF32;
};

template <>
struct TypeTagOf<double> {
  static constexpr TypeTag value = TypeTag::kF64;
};

template <>
struct TypeTagOf<bfloat16_t> {
  static constexpr TypeTag value = TypeTag::kBF16;
};

#if defined(__FLT16_MAX__)
template <>
struct TypeTagOf<_Float16> {
  static constexpr TypeTag value = TypeTag::kF16;
};
#endif

template <typename T>
inline constexpr TypeTag type_tag_of_v = TypeTagOf<T>::value;

}  // namespace base
}  // namespace cyber
}  // namespace world

#endif  // CYBER_BASE_TYPE_TAG_H_
