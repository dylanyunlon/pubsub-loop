/******************************************************************************
 * wire_format.h — Wire header formats for pub/sub message serialization
 *
 * PRD #233: V2 wire header for F16/BF16 messages, with version tag and
 * tick_id. V1 header remains unchanged for F32/F64 backward compatibility.
 *
 * Detection: V1 uses magic 0xB0B5, V2 uses 0xB0B6.
 * A V1 deserializer encountering 0xB0B6 applies TypeMismatchPolicy.
 * A V2 deserializer encountering 0xB0B5 falls through to V1 decode.
 *****************************************************************************/

#ifndef CYBER_BASE_WIRE_FORMAT_H_
#define CYBER_BASE_WIRE_FORMAT_H_

#include <cstdint>
#include <stdexcept>
#include <type_traits>

#include "cyber/base/type_tag.h"

namespace world {
namespace cyber {
namespace base {

// ─── Magic values ───────────────────────────────────────────────────────────

static constexpr uint16_t kWireMagicV1 = 0xB0B5;
static constexpr uint16_t kWireMagicV2 = 0xB0B6;

// ─── V1 wire header (8 bytes) ───────────────────────────────────────────────
//
// Used for F32 and F64 payloads. Unchanged from original format.
// [ magic(2) | type_tag(1) | reserved(1) | payload_len(4) ]

struct WireHeaderV1 {
  uint16_t magic;         // kWireMagicV1
  TypeTag  type_tag;
  uint8_t  reserved;
  uint32_t payload_len;
};
static_assert(sizeof(WireHeaderV1) == 8, "V1 header must be 8 bytes");
static_assert(std::is_standard_layout_v<WireHeaderV1>);

// ─── V2 wire header (16 bytes) ──────────────────────────────────────────────
//
// Used for F16 and BF16 payloads. Includes version tag and tick_id for
// deterministic replay (PRD #222 tick barrier compatibility).
// [ magic(2) | version(1) | type_flags(1) | payload_len(4) | tick_id(8) ]
//
// type_flags bit layout:
//   bit 0: F32 present (for mixed-type future extension)
//   bit 1: F64 present
//   bit 2: F16 present
//   bit 3: BF16 present
//   bits 4-7: reserved (must be 0)

struct WireHeaderV2 {
  uint16_t magic;         // kWireMagicV2
  uint8_t  version;       // 0x02
  uint8_t  type_flags;    // Bitmask of types in payload
  uint32_t payload_len;
  uint64_t tick_id;       // World tick at publish time
};
static_assert(sizeof(WireHeaderV2) == 16, "V2 header must be 16 bytes");
static_assert(std::is_standard_layout_v<WireHeaderV2>);

// type_flags bit positions
static constexpr uint8_t kTypeFlagF32  = 0x01;
static constexpr uint8_t kTypeFlagF64  = 0x02;
static constexpr uint8_t kTypeFlagF16  = 0x04;
static constexpr uint8_t kTypeFlagBF16 = 0x08;

// ─── TypeMismatchPolicy ─────────────────────────────────────────────────────

enum class TypeMismatchPolicy {
  kSkip,      // Silently discard; return std::nullopt (default)
  kError,     // Throw TypeMismatchError
  kFallback,  // Best-effort cast to requested type (lossy, debug only)
};

class TypeMismatchError : public std::runtime_error {
 public:
  TypeMismatchError(TypeTag expected, TypeTag actual)
      : std::runtime_error("Type mismatch in wire deserialization"),
        expected_(expected),
        actual_(actual) {}

  TypeMismatchError(uint16_t expected_magic, uint16_t actual_magic)
      : std::runtime_error("Wire magic mismatch"),
        expected_magic_(expected_magic),
        actual_magic_(actual_magic) {}

  TypeTag expected_type() const { return expected_; }
  TypeTag actual_type() const { return actual_; }

 private:
  TypeTag expected_{};
  TypeTag actual_{};
  uint16_t expected_magic_ = 0;
  uint16_t actual_magic_ = 0;
};

// ─── Header detection helpers ───────────────────────────────────────────────

/// Returns true if the buffer starts with a valid V1 magic.
inline bool is_v1_header(const void* data, size_t len) {
  if (len < sizeof(WireHeaderV1)) return false;
  uint16_t magic;
  std::memcpy(&magic, data, sizeof(magic));
  return magic == kWireMagicV1;
}

/// Returns true if the buffer starts with a valid V2 magic.
inline bool is_v2_header(const void* data, size_t len) {
  if (len < sizeof(WireHeaderV2)) return false;
  uint16_t magic;
  std::memcpy(&magic, data, sizeof(magic));
  return magic == kWireMagicV2;
}

/// Returns the wire format version (1, 2, or 0 for unknown).
inline int detect_wire_version(const void* data, size_t len) {
  if (len < 2) return 0;
  uint16_t magic;
  std::memcpy(&magic, data, sizeof(magic));
  if (magic == kWireMagicV1) return 1;
  if (magic == kWireMagicV2) return 2;
  return 0;
}

}  // namespace base
}  // namespace cyber
}  // namespace world

#endif  // CYBER_BASE_WIRE_FORMAT_H_
