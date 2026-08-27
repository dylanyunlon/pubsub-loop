/******************************************************************************
 * individual_state.h — Zero-copy individual state transport layout
 *
 * PRD #2: Fixed 64-byte header, custom_data points into ZeroCopyRegion SHM slot.
 * 
 * Layout:
 *   Identity:  individual_id(8) + tick_id(8) = 16 bytes
 *   Spatial:   position(12) + orientation(16) + velocity(12) = 40 bytes
 *   Meta:      payload_type(1) + flags(1) + custom_data_len(2) + checksum(4) = 8 bytes
 *   Total header: 64 bytes (cache-line aligned)
 *****************************************************************************/

#ifndef CYBER_TRANSPORT_INDIVIDUAL_STATE_H_
#define CYBER_TRANSPORT_INDIVIDUAL_STATE_H_

#include <cstdint>
#include <cstring>

namespace world {
namespace cyber {
namespace transport {

struct Vec3f {
  float x, y, z;
};

struct Quatf {
  float w, x, y, z;
};

enum class PayloadType : uint8_t {
  kNone = 0,
  kPhysics = 1,       // MotionRequest/ConfirmedState
  kSensor = 2,        // Sensor readings
  kGovernance = 3,    // Policy/audit data
  kCustom = 255,
};

/**
 * IndividualState — the unit of transport
 *
 * Designed for zero-copy SHM transfer:
 *   - 64-byte aligned for cache-line friendliness
 *   - Fixed header, variable custom_data via SHM pointer
 *   - publish_ns tracks wall-clock for latency measurement
 */
struct alignas(64) IndividualState {
  // ── Identity (16B) ──
  uint64_t individual_id;
  uint64_t tick_id;

  // ── Spatial (40B) ──
  Vec3f position;      // World-space position
  Quatf orientation;   // World-space rotation
  Vec3f velocity;      // Linear velocity

  // ── Meta (8B) ──
  PayloadType payload_type;
  uint8_t flags;
  uint16_t custom_data_len;
  uint32_t checksum;

  // ── Publish timestamp (8B) ──
  uint64_t publish_ns;

  // ── Custom data pointer (8B, into ZeroCopyRegion SHM slot) ──
  const void* custom_data;

  // Compile-time layout verification
  static_assert(sizeof(uint64_t) == 8, "uint64_t must be 8 bytes");
};

// Verify the critical offset
static_assert(offsetof(IndividualState, custom_data) == 80,
              "custom_data must be at offset 80 (64B header + 8B publish_ns + 8B padding)");
static_assert(sizeof(IndividualState) == 128 || sizeof(IndividualState) == 96,
              "IndividualState should fit in 1-2 cache lines");

/**
 * IndividualId — typed wrapper for individual identity
 */
struct IndividualId {
  uint64_t value;

  explicit IndividualId(uint64_t v = 0) : value(v) {}
  bool operator==(const IndividualId& o) const { return value == o.value; }
  bool operator!=(const IndividualId& o) const { return value != o.value; }
  bool operator<(const IndividualId& o) const { return value < o.value; }

  struct Hash {
    size_t operator()(const IndividualId& id) const {
      return std::hash<uint64_t>{}(id.value);
    }
  };
};

/**
 * Zero-copy region descriptor — identifies a SHM slot
 */
struct ZeroCopySlot {
  uint64_t region_id;       // Which SHM region
  uint32_t slot_offset;     // Offset within region
  uint32_t slot_size;       // Size of this slot
  uint64_t writer_seq;      // Sequence for ordering
};

}  // namespace transport
}  // namespace cyber
}  // namespace world

#endif  // CYBER_TRANSPORT_INDIVIDUAL_STATE_H_
