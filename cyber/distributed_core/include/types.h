/******************************************************************************
 * types.h — Unified platform-aligned integer type aliases for world runtime.
 *
 * Replaces scattered world_int32 / WInt32 / world::i32 definitions with a
 * single canonical header.  Handles the LP64 (Linux) vs LLP64 (Windows)
 * divergence and the 32-bit IPU device address space.
 *
 * PRD: #353 [DistributedCore] Align sized integer type aliases with platform
 *****************************************************************************/
#ifndef CYBER_DISTRIBUTED_CORE_TYPES_H_
#define CYBER_DISTRIBUTED_CORE_TYPES_H_

#include <cstddef>
#include <cstdint>

namespace world {

// ---- Signed integers (exact width) ----
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

// ---- Unsigned integers (exact width) ----
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// ---- Platform-adaptive size types ----
// GPU host: 64-bit.  IPU SM (device arch < 800): 32-bit.
#if defined(__CUDA_ARCH__) && (__CUDA_ARCH__ < 800)
using isize = i32;
using usize = u32;
#else
using isize = i64;
using usize = u64;
#endif

// Pointer-difference type, aligned with std::ptrdiff_t.
using ptrdiff = std::ptrdiff_t;

// ---- Floating-point shorthands ----
using f32 = float;
using f64 = double;

// ---- Compile-time guards ----
static_assert(sizeof(i32) == 4, "i32 must be exactly 4 bytes");
static_assert(sizeof(i64) == 8, "i64 must be exactly 8 bytes");
static_assert(sizeof(u8)  == 1, "u8 must be exactly 1 byte");
static_assert(sizeof(f32) == 4, "f32 must be exactly 4 bytes");
static_assert(sizeof(f64) == 8, "f64 must be exactly 8 bytes");
static_assert(sizeof(usize) == sizeof(void*) || sizeof(usize) == 4,
              "usize must match pointer width or be 32-bit on IPU device");

}  // namespace world

#endif  // CYBER_DISTRIBUTED_CORE_TYPES_H_
