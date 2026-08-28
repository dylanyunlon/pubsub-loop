/******************************************************************************
 * common_reference.h — C++17 common_reference_t with noexcept coverage
 *
 * PRD #748: Fix common_reference_t for function pointers with noexcept
 * specifier. C++17 made noexcept part of the type system, so
 * void(*)(int) and void(*)(int) noexcept are distinct types.
 *
 * This header provides partial specializations for the full cartesian
 * product of free function pointers and member function pointers:
 *   {plain, noexcept} × {const, volatile, const volatile} × {&, &&}
 *
 * Rule: noexcept always decays to non-noexcept (since noexcept is
 * a narrowing qualifier — every noexcept function IS-A non-noexcept).
 *****************************************************************************/

#ifndef CYBER_TOOLS_COMMON_REFERENCE_H_
#define CYBER_TOOLS_COMMON_REFERENCE_H_

#include <type_traits>

namespace world_model {
namespace tools {

// ─── Primary template ───────────────────────────────────────────────────────

template <typename T, typename U, typename = void>
struct common_reference;

// Same type → identity
template <typename T>
struct common_reference<T, T> {
  using type = T;
};

// ─── Free function pointers: noexcept → non-noexcept ────────────────────────

template <typename R, typename... Args>
struct common_reference<R(*)(Args...) noexcept, R(*)(Args...)> {
  using type = R(*)(Args...);
};

template <typename R, typename... Args>
struct common_reference<R(*)(Args...), R(*)(Args...) noexcept>
    : common_reference<R(*)(Args...) noexcept, R(*)(Args...)> {};

// ─── Member function pointers ───────────────────────────────────────────────
// Macro-generated cartesian product for cv-qualifiers

#define WORLD_COMMON_REF_MEMBER_NOEXCEPT(CV_QUAL)                        \
  template <typename R, typename C, typename... Args>                     \
  struct common_reference<R(C::*)(Args...) CV_QUAL noexcept,             \
                          R(C::*)(Args...) CV_QUAL> {                    \
    using type = R(C::*)(Args...) CV_QUAL;                               \
  };                                                                      \
  template <typename R, typename C, typename... Args>                     \
  struct common_reference<R(C::*)(Args...) CV_QUAL,                      \
                          R(C::*)(Args...) CV_QUAL noexcept>             \
      : common_reference<R(C::*)(Args...) CV_QUAL noexcept,             \
                          R(C::*)(Args...) CV_QUAL> {};

// Plain
WORLD_COMMON_REF_MEMBER_NOEXCEPT(/* empty */)
// const
WORLD_COMMON_REF_MEMBER_NOEXCEPT(const)
// volatile
WORLD_COMMON_REF_MEMBER_NOEXCEPT(volatile)
// const volatile
WORLD_COMMON_REF_MEMBER_NOEXCEPT(const volatile)

#undef WORLD_COMMON_REF_MEMBER_NOEXCEPT

// ─── Ref-qualified member function pointers ─────────────────────────────────

#define WORLD_COMMON_REF_MEMBER_NOEXCEPT_REF(CV_QUAL, REF_QUAL)          \
  template <typename R, typename C, typename... Args>                     \
  struct common_reference<R(C::*)(Args...) CV_QUAL REF_QUAL noexcept,    \
                          R(C::*)(Args...) CV_QUAL REF_QUAL> {           \
    using type = R(C::*)(Args...) CV_QUAL REF_QUAL;                      \
  };                                                                      \
  template <typename R, typename C, typename... Args>                     \
  struct common_reference<R(C::*)(Args...) CV_QUAL REF_QUAL,             \
                          R(C::*)(Args...) CV_QUAL REF_QUAL noexcept>    \
      : common_reference<R(C::*)(Args...) CV_QUAL REF_QUAL noexcept,    \
                          R(C::*)(Args...) CV_QUAL REF_QUAL> {};

// & qualified
WORLD_COMMON_REF_MEMBER_NOEXCEPT_REF(/* empty */, &)
WORLD_COMMON_REF_MEMBER_NOEXCEPT_REF(const, &)
WORLD_COMMON_REF_MEMBER_NOEXCEPT_REF(volatile, &)
WORLD_COMMON_REF_MEMBER_NOEXCEPT_REF(const volatile, &)

// && qualified
WORLD_COMMON_REF_MEMBER_NOEXCEPT_REF(/* empty */, &&)
WORLD_COMMON_REF_MEMBER_NOEXCEPT_REF(const, &&)
WORLD_COMMON_REF_MEMBER_NOEXCEPT_REF(volatile, &&)
WORLD_COMMON_REF_MEMBER_NOEXCEPT_REF(const volatile, &&)

#undef WORLD_COMMON_REF_MEMBER_NOEXCEPT_REF

// ─── Convenience alias ──────────────────────────────────────────────────────

template <typename T, typename U>
using common_reference_t = typename common_reference<T, U>::type;

// ─── SFINAE detection trait ─────────────────────────────────────────────────

template <typename T, typename U, typename = void>
struct has_common_reference : std::false_type {};

template <typename T, typename U>
struct has_common_reference<T, U,
    std::void_t<typename common_reference<T, U>::type>> : std::true_type {};

template <typename T, typename U>
inline constexpr bool has_common_reference_v =
    has_common_reference<T, U>::value;

}  // namespace tools
}  // namespace world_model

#endif  // CYBER_TOOLS_COMMON_REFERENCE_H_
