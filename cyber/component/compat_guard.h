/******************************************************************************
 * compat_guard.h — Compile-time compatibility guard for component headers
 *
 * PRD #243: Prevents confusing link errors on embedded Cyber RT runtimes
 * by detecting CYBER_RT_EMBEDDED at include time and emitting a clear
 * static_assert with migration guidance.
 *
 * Usage in GPU-dependent headers:
 *   #include "cyber/component/compat_guard.h"
 *   WORLD_COMPONENT_REQUIRES_FULL_CYBER_RT(
 *       "tensor_parallel.h",
 *       "CPU-only: cyber/component/tensor_cpu.h");
 *
 * When CYBER_RT_EMBEDDED is defined, this triggers:
 *   error: static_assert failed: "[world::component] tensor_parallel.h
 *     requires full Cyber RT with GPU support. ..."
 *
 * When not defined: expands to nothing (zero overhead).
 *****************************************************************************/

#ifndef CYBER_COMPONENT_COMPAT_GUARD_H_
#define CYBER_COMPONENT_COMPAT_GUARD_H_

// ═══════════════════════════════════════════════════════════════════════════════
// Feature detection macros
// ═══════════════════════════════════════════════════════════════════════════════

// Is this an embedded (no-GPU) Cyber RT runtime?
#if defined(CYBER_RT_EMBEDDED) && CYBER_RT_EMBEDDED
  #define WORLD_IS_EMBEDDED_RUNTIME 1
#else
  #define WORLD_IS_EMBEDDED_RUNTIME 0
#endif

// Is CUDA compilation available?
#if defined(__CUDACC__) || defined(CUDA_VERSION)
  #define WORLD_HAS_CUDA 1
#else
  #define WORLD_HAS_CUDA 0
#endif

// Is NVTX profiling available?
#if defined(NVTX_VERSION) || defined(__NVTX__)
  #define WORLD_HAS_NVTX 1
#else
  #define WORLD_HAS_NVTX 0
#endif

// ═══════════════════════════════════════════════════════════════════════════════
// Guard macros
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * WORLD_COMPONENT_REQUIRES_FULL_CYBER_RT(header, alternatives)
 *
 * Place at the top of any header that depends on full Cyber RT (GPU services,
 * CUDA runtime, etc.). On embedded runtimes, produces a clear static_assert.
 *
 * @param header_name    Name of the current header file (string literal)
 * @param alternatives   Available CPU-only alternatives (string literal)
 */
#if WORLD_IS_EMBEDDED_RUNTIME

  // On embedded: fire a static_assert with migration guidance.
  // Using a template trick to include the header name in the message,
  // since static_assert(false, ...) in a non-template context fires
  // unconditionally (which is what we want here).
  #define WORLD_COMPONENT_REQUIRES_FULL_CYBER_RT(header_name, alternatives) \
    static_assert(!WORLD_IS_EMBEDDED_RUNTIME,                               \
      "[world::component] " header_name " requires full Cyber RT with "     \
      "GPU/CUDA support. Embedded Cyber RT (CYBER_RT_EMBEDDED=1) is "       \
      "detected. Alternatives: " alternatives ". "                          \
      "Migration guide: world/component/docs/embedded_compat.md")

#else

  // On full runtime: no-op
  #define WORLD_COMPONENT_REQUIRES_FULL_CYBER_RT(header_name, alternatives) \
    /* Full Cyber RT — no restrictions */

#endif

/**
 * WORLD_COMPONENT_REQUIRES_CUDA(header, alternatives)
 *
 * Place at the top of any header that requires CUDA compilation context.
 * Fires when the header is included from a non-CUDA translation unit
 * (e.g., a .cpp file compiled with GCC instead of nvcc).
 */
#if !WORLD_HAS_CUDA

  #define WORLD_COMPONENT_REQUIRES_CUDA(header_name, alternatives)          \
    static_assert(WORLD_HAS_CUDA,                                           \
      "[world::component] " header_name " requires CUDA compilation "       \
      "(__CUDACC__ not defined). Compile with nvcc or include from a "       \
      ".cu file. CPU-only alternatives: " alternatives ".")

#else

  #define WORLD_COMPONENT_REQUIRES_CUDA(header_name, alternatives) \
    /* CUDA available — no restrictions */

#endif

/**
 * WORLD_COMPONENT_REQUIRES_NVTX(header)
 *
 * Guard for headers that depend on NVTX profiling SDK.
 */
#if !WORLD_HAS_NVTX

  #define WORLD_COMPONENT_REQUIRES_NVTX(header_name)                        \
    static_assert(WORLD_HAS_NVTX,                                           \
      "[world::component] " header_name " requires NVTX SDK. "             \
      "Use cyber/component/null_profiler.h for a no-op profiler stub.")

#else

  #define WORLD_COMPONENT_REQUIRES_NVTX(header_name) \
    /* NVTX available — no restrictions */

#endif

// ═══════════════════════════════════════════════════════════════════════════════
// Runtime feature query (for conditional dispatch at runtime)
// ═══════════════════════════════════════════════════════════════════════════════

namespace world {
namespace cyber {
namespace component {

/// Compile-time query: is this an embedded runtime?
inline constexpr bool kIsEmbeddedRuntime = WORLD_IS_EMBEDDED_RUNTIME;

/// Compile-time query: is CUDA available?
inline constexpr bool kHasCuda = WORLD_HAS_CUDA;

/// Compile-time query: is NVTX available?
inline constexpr bool kHasNvtx = WORLD_HAS_NVTX;

}  // namespace component
}  // namespace cyber
}  // namespace world

#endif  // CYBER_COMPONENT_COMPAT_GUARD_H_
