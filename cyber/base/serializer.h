/******************************************************************************
 * serializer.h — Scalar and batch serialization for pub/sub wire transport
 *
 * PRD #233: Adds BF16 and F16 serialization specializations with V2 wire
 * header. V1 F32/F64 paths remain unchanged. TypeMismatchPolicy governs
 * behavior when a deserializer encounters a wire format it doesn't expect.
 *
 * Thread safety: All serialize functions produce independent Buffer outputs
 * with no shared mutable state. Safe for concurrent use from multiple
 * ChannelWriter threads.
 *****************************************************************************/

#ifndef CYBER_BASE_SERIALIZER_H_
#define CYBER_BASE_SERIALIZER_H_

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "cyber/base/type_tag.h"
#include "cyber/base/wire_format.h"

namespace world {
namespace cyber {
namespace base {

// ─── Buffer ─────────────────────────────────────────────────────────────────
//
// Simple owned byte buffer for serialized payloads.
// In production this would be backed by the SHM arena allocator;
// here we use std::vector<uint8_t> for portability.

class Buffer {
 public:
  Buffer() = default;
  explicit Buffer(size_t size) : data_(size) {}
  Buffer(const uint8_t* data, size_t size) : data_(data, data + size) {}

  uint8_t* data() { return data_.data(); }
  const uint8_t* data() const { return data_.data(); }
  size_t size() const { return data_.size(); }
  bool empty() const { return data_.empty(); }

  void resize(size_t n) { data_.resize(n); }

 private:
  std::vector<uint8_t> data_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Scalar serialize / deserialize
// ═══════════════════════════════════════════════════════════════════════════════

// Forward declarations — specializations below
template <typename T>
Buffer serialize(const T& value);

template <typename T>
std::optional<T> deserialize_safe(const Buffer& buf,
                                   TypeMismatchPolicy policy
                                       = TypeMismatchPolicy::kSkip);

// ─── F32 (V1) ───────────────────────────────────────────────────────────────

template <>
inline Buffer serialize<float>(const float& value) {
  Buffer buf(sizeof(WireHeaderV1) + sizeof(float));
  auto* hdr = reinterpret_cast<WireHeaderV1*>(buf.data());
  hdr->magic       = kWireMagicV1;
  hdr->type_tag    = TypeTag::kF32;
  hdr->reserved    = 0;
  hdr->payload_len = sizeof(float);
  std::memcpy(buf.data() + sizeof(WireHeaderV1), &value, sizeof(float));
  return buf;
}

template <>
inline std::optional<float> deserialize_safe<float>(
    const Buffer& buf, TypeMismatchPolicy policy) {
  int ver = detect_wire_version(buf.data(), buf.size());

  if (ver == 1) {
    if (buf.size() < sizeof(WireHeaderV1) + sizeof(float)) return std::nullopt;
    auto* hdr = reinterpret_cast<const WireHeaderV1*>(buf.data());
    if (hdr->type_tag != TypeTag::kF32) {
      if (hdr->type_tag == TypeTag::kF64 && policy == TypeMismatchPolicy::kFallback) {
        // Lossy: truncate f64 to f32
        double d;
        std::memcpy(&d, buf.data() + sizeof(WireHeaderV1), sizeof(double));
        return static_cast<float>(d);
      }
      if (policy == TypeMismatchPolicy::kError) {
        throw TypeMismatchError(TypeTag::kF32, hdr->type_tag);
      }
      return std::nullopt;  // kSkip
    }
    float result;
    std::memcpy(&result, buf.data() + sizeof(WireHeaderV1), sizeof(float));
    return result;
  }

  if (ver == 2) {
    // V2 message received by F32 deserializer — type mismatch
    if (policy == TypeMismatchPolicy::kFallback) {
      // Attempt BF16→float fallback
      auto* hdr = reinterpret_cast<const WireHeaderV2*>(buf.data());
      if ((hdr->type_flags & kTypeFlagBF16) &&
          buf.size() >= sizeof(WireHeaderV2) + sizeof(uint16_t)) {
        bfloat16_t bf;
        std::memcpy(&bf.bits, buf.data() + sizeof(WireHeaderV2),
                     sizeof(uint16_t));
        return bf.to_float();
      }
    }
    if (policy == TypeMismatchPolicy::kError) {
      throw TypeMismatchError(kWireMagicV1, kWireMagicV2);
    }
    return std::nullopt;  // kSkip — safe discard of V2 message
  }

  return std::nullopt;  // Unknown format
}

// ─── F64 (V1) ───────────────────────────────────────────────────────────────

template <>
inline Buffer serialize<double>(const double& value) {
  Buffer buf(sizeof(WireHeaderV1) + sizeof(double));
  auto* hdr = reinterpret_cast<WireHeaderV1*>(buf.data());
  hdr->magic       = kWireMagicV1;
  hdr->type_tag    = TypeTag::kF64;
  hdr->reserved    = 0;
  hdr->payload_len = sizeof(double);
  std::memcpy(buf.data() + sizeof(WireHeaderV1), &value, sizeof(double));
  return buf;
}

template <>
inline std::optional<double> deserialize_safe<double>(
    const Buffer& buf, TypeMismatchPolicy policy) {
  int ver = detect_wire_version(buf.data(), buf.size());

  if (ver == 1) {
    if (buf.size() < sizeof(WireHeaderV1) + sizeof(double)) return std::nullopt;
    auto* hdr = reinterpret_cast<const WireHeaderV1*>(buf.data());
    if (hdr->type_tag != TypeTag::kF64) {
      if (policy == TypeMismatchPolicy::kError) {
        throw TypeMismatchError(TypeTag::kF64, hdr->type_tag);
      }
      return std::nullopt;
    }
    double result;
    std::memcpy(&result, buf.data() + sizeof(WireHeaderV1), sizeof(double));
    return result;
  }

  if (ver == 2) {
    if (policy == TypeMismatchPolicy::kError) {
      throw TypeMismatchError(kWireMagicV1, kWireMagicV2);
    }
    return std::nullopt;
  }

  return std::nullopt;
}

// ─── BF16 (V2) ──────────────────────────────────────────────────────────────

template <>
inline Buffer serialize<bfloat16_t>(const bfloat16_t& value) {
  Buffer buf(sizeof(WireHeaderV2) + sizeof(uint16_t));
  auto* hdr = reinterpret_cast<WireHeaderV2*>(buf.data());
  hdr->magic       = kWireMagicV2;
  hdr->version     = 0x02;
  hdr->type_flags  = kTypeFlagBF16;
  hdr->payload_len = sizeof(uint16_t);
  hdr->tick_id     = 0;  // Caller (transport layer) overwrites at commit time
  std::memcpy(buf.data() + sizeof(WireHeaderV2), &value.bits, sizeof(uint16_t));
  return buf;
}

template <>
inline std::optional<bfloat16_t> deserialize_safe<bfloat16_t>(
    const Buffer& buf, TypeMismatchPolicy policy) {
  int ver = detect_wire_version(buf.data(), buf.size());

  if (ver == 2) {
    if (buf.size() < sizeof(WireHeaderV2) + sizeof(uint16_t))
      return std::nullopt;
    auto* hdr = reinterpret_cast<const WireHeaderV2*>(buf.data());
    if (!(hdr->type_flags & kTypeFlagBF16)) {
      if (policy == TypeMismatchPolicy::kError) {
        throw TypeMismatchError(TypeTag::kBF16, TypeTag::kF32);
      }
      return std::nullopt;
    }
    bfloat16_t result;
    std::memcpy(&result.bits, buf.data() + sizeof(WireHeaderV2),
                sizeof(uint16_t));
    return result;
  }

  if (ver == 1) {
    // V1 message received by BF16 deserializer
    if (policy == TypeMismatchPolicy::kFallback) {
      // Attempt float→BF16 lossy conversion
      auto opt_f = deserialize_safe<float>(buf, TypeMismatchPolicy::kFallback);
      if (opt_f.has_value()) {
        return bfloat16_t::from_float(*opt_f);
      }
    }
    if (policy == TypeMismatchPolicy::kError) {
      throw TypeMismatchError(kWireMagicV2, kWireMagicV1);
    }
    return std::nullopt;
  }

  return std::nullopt;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Batch serialize / deserialize
// ═══════════════════════════════════════════════════════════════════════════════

// ─── BF16 batch ─────────────────────────────────────────────────────────────
//
// Zero-copy bit layout into wire buffer. Each bfloat16_t is 2 bytes;
// the batch is packed contiguously after the V2 header.
// No static_cast<float> anywhere in this path.

inline Buffer serialize_batch_bf16(std::span<const bfloat16_t> values) {
  const size_t payload_bytes = values.size() * sizeof(uint16_t);
  Buffer buf(sizeof(WireHeaderV2) + payload_bytes);

  auto* hdr = reinterpret_cast<WireHeaderV2*>(buf.data());
  hdr->magic       = kWireMagicV2;
  hdr->version     = 0x02;
  hdr->type_flags  = kTypeFlagBF16;
  hdr->payload_len = static_cast<uint32_t>(payload_bytes);
  hdr->tick_id     = 0;

  // Direct memcpy of uint16_t bits — no float conversion
  std::memcpy(buf.data() + sizeof(WireHeaderV2), values.data(), payload_bytes);
  return buf;
}

/// Deserialize a BF16 batch. Returns element count via out_count.
inline bool deserialize_batch_bf16(const Buffer& buf,
                                    std::span<bfloat16_t> out,
                                    size_t* out_count,
                                    TypeMismatchPolicy policy
                                        = TypeMismatchPolicy::kSkip) {
  if (!is_v2_header(buf.data(), buf.size())) {
    if (policy == TypeMismatchPolicy::kError) {
      throw TypeMismatchError(kWireMagicV2, kWireMagicV1);
    }
    if (out_count) *out_count = 0;
    return false;
  }

  auto* hdr = reinterpret_cast<const WireHeaderV2*>(buf.data());
  if (!(hdr->type_flags & kTypeFlagBF16)) {
    if (out_count) *out_count = 0;
    return false;
  }

  size_t n_elements = hdr->payload_len / sizeof(uint16_t);
  size_t copy_count = (n_elements < out.size()) ? n_elements : out.size();

  std::memcpy(out.data(), buf.data() + sizeof(WireHeaderV2),
              copy_count * sizeof(uint16_t));

  if (out_count) *out_count = copy_count;
  return true;
}

// ─── F16 batch (compiler-gated) ─────────────────────────────────────────────

#if defined(__FLT16_MAX__)

template <>
inline Buffer serialize<_Float16>(const _Float16& value) {
  Buffer buf(sizeof(WireHeaderV2) + sizeof(_Float16));
  auto* hdr = reinterpret_cast<WireHeaderV2*>(buf.data());
  hdr->magic       = kWireMagicV2;
  hdr->version     = 0x02;
  hdr->type_flags  = kTypeFlagF16;
  hdr->payload_len = sizeof(_Float16);
  hdr->tick_id     = 0;
  std::memcpy(buf.data() + sizeof(WireHeaderV2), &value, sizeof(_Float16));
  return buf;
}

template <>
inline std::optional<_Float16> deserialize_safe<_Float16>(
    const Buffer& buf, TypeMismatchPolicy policy) {
  if (!is_v2_header(buf.data(), buf.size())) {
    if (policy == TypeMismatchPolicy::kError) {
      throw TypeMismatchError(kWireMagicV2, kWireMagicV1);
    }
    return std::nullopt;
  }

  auto* hdr = reinterpret_cast<const WireHeaderV2*>(buf.data());
  if (!(hdr->type_flags & kTypeFlagF16)) {
    if (policy == TypeMismatchPolicy::kError) {
      throw TypeMismatchError(TypeTag::kF16, TypeTag::kF32);
    }
    return std::nullopt;
  }

  if (buf.size() < sizeof(WireHeaderV2) + sizeof(_Float16)) return std::nullopt;

  _Float16 result;
  std::memcpy(&result, buf.data() + sizeof(WireHeaderV2), sizeof(_Float16));
  return result;
}

inline Buffer serialize_batch_f16(std::span<const _Float16> values) {
  const size_t payload_bytes = values.size() * sizeof(_Float16);
  Buffer buf(sizeof(WireHeaderV2) + payload_bytes);

  auto* hdr = reinterpret_cast<WireHeaderV2*>(buf.data());
  hdr->magic       = kWireMagicV2;
  hdr->version     = 0x02;
  hdr->type_flags  = kTypeFlagF16;
  hdr->payload_len = static_cast<uint32_t>(payload_bytes);
  hdr->tick_id     = 0;

  // Direct memcpy — _Float16 is 2 bytes, IEEE 754 binary16
  std::memcpy(buf.data() + sizeof(WireHeaderV2), values.data(), payload_bytes);
  return buf;
}

inline bool deserialize_batch_f16(const Buffer& buf,
                                   std::span<_Float16> out,
                                   size_t* out_count,
                                   TypeMismatchPolicy policy
                                       = TypeMismatchPolicy::kSkip) {
  if (!is_v2_header(buf.data(), buf.size())) {
    if (policy == TypeMismatchPolicy::kError) {
      throw TypeMismatchError(kWireMagicV2, kWireMagicV1);
    }
    if (out_count) *out_count = 0;
    return false;
  }

  auto* hdr = reinterpret_cast<const WireHeaderV2*>(buf.data());
  if (!(hdr->type_flags & kTypeFlagF16)) {
    if (out_count) *out_count = 0;
    return false;
  }

  size_t n_elements = hdr->payload_len / sizeof(_Float16);
  size_t copy_count = (n_elements < out.size()) ? n_elements : out.size();

  std::memcpy(out.data(), buf.data() + sizeof(WireHeaderV2),
              copy_count * sizeof(_Float16));

  if (out_count) *out_count = copy_count;
  return true;
}

#endif  // __FLT16_MAX__

}  // namespace base
}  // namespace cyber
}  // namespace world

#endif  // CYBER_BASE_SERIALIZER_H_
