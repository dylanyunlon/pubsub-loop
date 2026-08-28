/******************************************************************************
 * math_constants.h — Unified math constants for CPU + GPU code paths
 *
 * PRD #238: Replace fragmented M_PI / local kPi / runtime acos(-1) with
 * a single constexpr header that works across:
 *   - CUDA device code (via cuda::std::numbers)
 *   - C++20 host code (via std::numbers)
 *   - C++17 host code (via long double literals)
 *   - MSVC (no _USE_MATH_DEFINES needed)
 *
 * All constants are constexpr and __host__ __device__ compatible.
 *****************************************************************************/

#ifndef CYBER_BASE_MATH_CONSTANTS_H_
#define CYBER_BASE_MATH_CONSTANTS_H_

// ─── Source selection ───────────────────────────────────────────────────────

#if defined(__CUDACC__)
  // CUDA path: use libcu++ cuda::std::numbers
  #include <cuda/std/numbers>
  #define WORLD_MATH_SRC_CUDA 1
#elif __cplusplus >= 202002L
  // C++20 path: use std::numbers
  #include <numbers>
  #define WORLD_MATH_SRC_CXX20 1
#else
  // C++17 fallback: long double literals truncated to T
  #define WORLD_MATH_SRC_FALLBACK 1
#endif

namespace world {
namespace cyber {
namespace base {
namespace math {

// ─── Primary variable templates ─────────────────────────────────────────────

#if defined(WORLD_MATH_SRC_CUDA)

template <typename T> inline constexpr T pi       = cuda::std::numbers::pi_v<T>;
template <typename T> inline constexpr T e        = cuda::std::numbers::e_v<T>;
template <typename T> inline constexpr T sqrt2    = cuda::std::numbers::sqrt2_v<T>;
template <typename T> inline constexpr T sqrt3    = cuda::std::numbers::sqrt3_v<T>;
template <typename T> inline constexpr T ln2      = cuda::std::numbers::ln2_v<T>;
template <typename T> inline constexpr T ln10     = cuda::std::numbers::ln10_v<T>;
template <typename T> inline constexpr T inv_pi   = cuda::std::numbers::inv_pi_v<T>;
template <typename T> inline constexpr T inv_sqrt2= T(0.70710678118654752440L);
template <typename T> inline constexpr T phi      = T(1.61803398874989484820L);

#elif defined(WORLD_MATH_SRC_CXX20)

template <typename T> inline constexpr T pi       = std::numbers::pi_v<T>;
template <typename T> inline constexpr T e        = std::numbers::e_v<T>;
template <typename T> inline constexpr T sqrt2    = std::numbers::sqrt2_v<T>;
template <typename T> inline constexpr T sqrt3    = std::numbers::sqrt3_v<T>;
template <typename T> inline constexpr T ln2      = std::numbers::ln2_v<T>;
template <typename T> inline constexpr T ln10     = std::numbers::ln10_v<T>;
template <typename T> inline constexpr T inv_pi   = std::numbers::inv_pi_v<T>;
template <typename T> inline constexpr T inv_sqrt2= std::numbers::inv_sqrtpi_v<T>;
template <typename T> inline constexpr T phi      = std::numbers::phi_v<T>;

#else  // WORLD_MATH_SRC_FALLBACK (C++17)

template <typename T> inline constexpr T pi       = T(3.14159265358979323846264338327950288L);
template <typename T> inline constexpr T e        = T(2.71828182845904523536028747135266250L);
template <typename T> inline constexpr T sqrt2    = T(1.41421356237309504880168872420969808L);
template <typename T> inline constexpr T sqrt3    = T(1.73205080756887729352744634150587237L);
template <typename T> inline constexpr T ln2      = T(0.69314718055994530941723212145817657L);
template <typename T> inline constexpr T ln10     = T(2.30258509299404568401799145468436421L);
template <typename T> inline constexpr T inv_pi   = T(0.31830988618379067153776752674502872L);
template <typename T> inline constexpr T inv_sqrt2= T(0.70710678118654752440084436210484904L);
template <typename T> inline constexpr T phi      = T(1.61803398874989484820458683436563812L);

#endif

// ─── Convenience aliases ────────────────────────────────────────────────────

// Double-precision (most common in world-space transforms)
inline constexpr double pi_d       = pi<double>;
inline constexpr double e_d        = e<double>;
inline constexpr double sqrt2_d    = sqrt2<double>;
inline constexpr double inv_pi_d   = inv_pi<double>;

// Single-precision (GPU kernels, quaternion math)
inline constexpr float  pi_f       = pi<float>;
inline constexpr float  e_f        = e<float>;
inline constexpr float  sqrt2_f    = sqrt2<float>;
inline constexpr float  inv_pi_f   = inv_pi<float>;

// Derived constants
inline constexpr double two_pi     = 2.0 * pi_d;
inline constexpr float  two_pi_f   = 2.0f * pi_f;
inline constexpr double half_pi    = 0.5 * pi_d;
inline constexpr float  half_pi_f  = 0.5f * pi_f;
inline constexpr double deg2rad    = pi_d / 180.0;
inline constexpr double rad2deg    = 180.0 / pi_d;
inline constexpr float  deg2rad_f  = pi_f / 180.0f;
inline constexpr float  rad2deg_f  = 180.0f / pi_f;

// ─── constexpr conversion functions ─────────────────────────────────────────

/// Convert degrees to radians (constexpr, __host__ __device__ safe).
template <typename T>
constexpr T degrees_to_radians(T deg) {
  return deg * pi<T> / T(180);
}

/// Convert radians to degrees (constexpr, __host__ __device__ safe).
template <typename T>
constexpr T radians_to_degrees(T rad) {
  return rad * T(180) / pi<T>;
}

}  // namespace math
}  // namespace base
}  // namespace cyber
}  // namespace world

// Cleanup preprocessor
#undef WORLD_MATH_SRC_CUDA
#undef WORLD_MATH_SRC_CXX20
#undef WORLD_MATH_SRC_FALLBACK

#endif  // CYBER_BASE_MATH_CONSTANTS_H_
