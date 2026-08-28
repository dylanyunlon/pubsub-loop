/******************************************************************************
 * individual_state.h — Finalized v1.0 IndividualState message schema
 *
 * PRD #228: Lock in the canonical layout for all pub/sub-loop consumers.
 * This struct MUST remain binary-compatible across v1.x releases.
 * Fields may ONLY be added into _reserved slots at the END.
 *
 * Layout: 256 bytes, alignas(64), standard-layout, trivially-copyable.
 *   Block 1: Metadata  (32 bytes, offset 0)
 *   Block 2: Spatial   (52 bytes, offset 32)
 *   Block 3: Scheduler (4 bytes, offset 84)
 *   Block 4: Payload   (164 bytes, offset 88)
 *   Block 5: Reserved  (4 bytes, offset 252)
 *   Total: 256 bytes
 *
 * Wire formats:
 *   - DDS CDR (little-endian): direct memcpy of struct layout
 *   - protobuf (nanopb): field numbers match individual_state.proto
 *   - Packed binary (ARM): minimal subset {id, position, velocity}
 *
 * Note: this is the world::message schema for the finalized v1.0 format.
 * The transport::IndividualState in transport/individual_state.h is the
 * L1 transport wire header (64-byte, Vec3f floats). This message schema
 * is the L2 semantic format consumed by scheduler, data, and node layers.
 *****************************************************************************/

#ifndef CYBER_MESSAGE_INDIVIDUAL_STATE_H_
#define CYBER_MESSAGE_INDIVIDUAL_STATE_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace world {
namespace cyber {
namespace message {

// ─── Schema version sentinel ────────────────────────────────────────────────

/// v1.0.0 — breaking change from pre-v1.0 (float→double position,
/// uint32→uint64 sequence_number).
inline constexpr uint32_t INDIVIDUAL_STATE_SCHEMA_VERSION = 0x00010000;

// ─── Status flags ───────────────────────────────────────────────────────────

namespace status {
  inline constexpr uint32_t kAlive     = 0x00000000;
  inline constexpr uint32_t kDeparting = 0x00000001;
  inline constexpr uint32_t kError     = 0x00000002;
  inline constexpr uint32_t kFrozen    = 0x00000004;  // Paused by governance
}  // namespace status

// ─── IndividualState v1.0 ───────────────────────────────────────────────────

struct alignas(64) IndividualState {
  // ── Metadata block (32 bytes, offset 0) ──
  uint64_t individual_id;       // offset 0:  globally unique, assigned at birth
  uint64_t sequence_number;     // offset 8:  monotonic per individual (uint64!)
  uint64_t timestamp_ns;        // offset 16: nanoseconds since TAI epoch
  uint32_t source_node_id;      // offset 24: originating Cyber RT node
  uint32_t status_flags;        // offset 28: see status:: constants

  // ── Spatial block (52 bytes, offset 32) ──
  double   position_x;          // offset 32: meters, WGS84 or local frame
  double   position_y;          // offset 40
  double   position_z;          // offset 48
  float    orientation_w;       // offset 56: quaternion (w,x,y,z)
  float    orientation_x;       // offset 60
  float    orientation_y;       // offset 64
  float    orientation_z;       // offset 68
  float    velocity_x;          // offset 72: m/s
  float    velocity_y;          // offset 76
  float    velocity_z;          // offset 80

  // ── Scheduler block (4 bytes, offset 84) ──
  float    priority;            // offset 84: scheduler priority weight

  // ── Payload block (164 bytes, offset 88) ──
  uint32_t payload_size;        // offset 88: actual size of payload data
  uint8_t  payload[160];        // offset 92: in-struct extension payload

  // ── Reserved (4 bytes, offset 252) ──
  uint8_t  _reserved[4];        // offset 252: future v1.x additions
};

// ─── Compile-time layout verification ───────────────────────────────────────

static_assert(sizeof(IndividualState) == 256,
    "IndividualState must be exactly 256 bytes for GPU register alignment");
static_assert(alignof(IndividualState) == 64,
    "IndividualState must be 64-byte aligned (cache-line)");
static_assert(std::is_standard_layout_v<IndividualState>,
    "IndividualState must be standard-layout for DDS CDR and zero-copy SHM");
static_assert(std::is_trivially_copyable_v<IndividualState>,
    "IndividualState must be trivially copyable for GPU memcpy");

// Offset verification (matches proto field numbering)
static_assert(offsetof(IndividualState, individual_id) == 0);
static_assert(offsetof(IndividualState, sequence_number) == 8);
static_assert(offsetof(IndividualState, timestamp_ns) == 16);
static_assert(offsetof(IndividualState, source_node_id) == 24);
static_assert(offsetof(IndividualState, status_flags) == 28);
static_assert(offsetof(IndividualState, position_x) == 32);
static_assert(offsetof(IndividualState, position_y) == 40);
static_assert(offsetof(IndividualState, position_z) == 48);
static_assert(offsetof(IndividualState, orientation_w) == 56);
static_assert(offsetof(IndividualState, velocity_x) == 72);
static_assert(offsetof(IndividualState, priority) == 84);
static_assert(offsetof(IndividualState, payload_size) == 88);
static_assert(offsetof(IndividualState, payload) == 92);

// ─── Legacy migration ───────────────────────────────────────────────────────

/// Pre-v1.0 layout (for migration utilities)
struct LegacyIndividualState {
  uint64_t id;
  uint32_t seq;             // was uint32_t
  float    pos[3];          // was float[3]
  float    orient[4];
  float    vel[3];
  uint8_t  flags;
  uint8_t  _pad[3];
  uint64_t publish_ns;
  const void* custom_data;
};

/// Migrate a pre-v1.0 state to the v1.0 schema.
/// Handles float→double position upgrade and uint32→uint64 sequence_number.
inline IndividualState migrate_v0_to_v1(const LegacyIndividualState& old) {
  IndividualState s{};
  s.individual_id   = old.id;
  s.sequence_number = static_cast<uint64_t>(old.seq);
  s.timestamp_ns    = old.publish_ns;
  s.source_node_id  = 0;  // Not available in legacy format
  s.status_flags    = static_cast<uint32_t>(old.flags);
  s.position_x      = static_cast<double>(old.pos[0]);
  s.position_y      = static_cast<double>(old.pos[1]);
  s.position_z      = static_cast<double>(old.pos[2]);
  s.orientation_w   = old.orient[0];
  s.orientation_x   = old.orient[1];
  s.orientation_y   = old.orient[2];
  s.orientation_z   = old.orient[3];
  s.velocity_x      = old.vel[0];
  s.velocity_y      = old.vel[1];
  s.velocity_z      = old.vel[2];
  s.priority        = 0.0f;
  s.payload_size    = 0;
  return s;
}

// ─── Utility ────────────────────────────────────────────────────────────────

/// Zero-initialize a state with a given ID.
inline IndividualState make_individual_state(uint64_t id) {
  IndividualState s{};
  s.individual_id  = id;
  s.orientation_w  = 1.0f;  // Identity quaternion
  s.status_flags   = status::kAlive;
  return s;
}

}  // namespace message
}  // namespace cyber
}  // namespace world

#endif  // CYBER_MESSAGE_INDIVIDUAL_STATE_H_
