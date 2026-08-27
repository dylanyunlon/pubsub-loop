/******************************************************************************
 * demangle.h — Portable C++ symbol demangling utility
 *
 * PRD #59: Unified demangle function for profiler, logger, and tools.
 * Replaces 7+ scattered raw __cxa_demangle calls with a safe wrapper that:
 *   - Uses RAII for malloc'd buffer (no leak possible)
 *   - Handles GCC/Clang (__cxa_demangle), MSVC (typeid prefix strip), and
 *     unknown platforms (returns mangled name as-is)
 *   - Off-path utility: NOT on the pub/sub hot path, only used for debug
 *     output and profiler stack traces
 *
 * Sources:
 *   - Apollo CyberRT: plugin_manager/plugin_manager.h (raw __cxa_demangle)
 *   - agent-governance-toolkit: (no demangle — this is CyberRT-specific)
 *
 * Usage:
 *   std::string readable = world::cyber::common::demangle(
 *       "_ZN9transport14UnifiedWriterI15IndividualStateE5writeERKS1_");
 *   // → "transport::UnifiedWriter<IndividualState>::write(IndividualState const&)"
 *
 *   // Type-based convenience:
 *   std::string name = world::cyber::common::demangle<IndividualState>();
 *   // → "world::cyber::transport::IndividualState"
 *
 * Namespace: world::cyber::common
 *****************************************************************************/

#ifndef CYBER_COMMON_DEMANGLE_H_
#define CYBER_COMMON_DEMANGLE_H_

#include <cstdlib>
#include <memory>
#include <string>
#include <typeinfo>

#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#define CYBER_HAS_CXA_DEMANGLE 1
#elif defined(_MSC_VER)
#define CYBER_HAS_CXA_DEMANGLE 0
#else
#define CYBER_HAS_CXA_DEMANGLE 0
#endif

namespace world {
namespace cyber {
namespace common {

/**
 * Demangle a C++ mangled symbol name.
 *
 * @param mangled  The mangled name (e.g. from backtrace_symbols or typeid)
 * @return Demangled human-readable name, or the original string if
 *         demangling fails or is unsupported.
 *
 * Thread-safe. No global state. The returned string owns its memory.
 */
inline std::string demangle(const char* mangled) {
  if (!mangled || mangled[0] == '\0') {
    return {};
  }

#if CYBER_HAS_CXA_DEMANGLE
  // GCC / Clang: abi::__cxa_demangle
  // RAII wrapper for the malloc'd buffer — unique_ptr with free as deleter
  int status = -1;
  std::unique_ptr<char, decltype(&std::free)> demangled(
      abi::__cxa_demangle(mangled, nullptr, nullptr, &status),
      std::free);

  if (status == 0 && demangled) {
    return std::string(demangled.get());
  }
  // status != 0: not a valid mangled name, return as-is
  return std::string(mangled);

#elif defined(_MSC_VER)
  // MSVC: typeid().name() returns "class Foo" or "struct Bar" —
  // strip the leading type-key prefix.
  std::string name(mangled);
  // Remove common MSVC prefixes
  static const char* prefixes[] = {
      "class ", "struct ", "enum ", "union ",
  };
  for (const char* prefix : prefixes) {
    std::string p(prefix);
    if (name.compare(0, p.size(), p) == 0) {
      name = name.substr(p.size());
      break;
    }
  }
  return name;

#else
  // Unknown platform: return as-is
  return std::string(mangled);
#endif
}

/**
 * Overload taking std::string.
 */
inline std::string demangle(const std::string& mangled) {
  return demangle(mangled.c_str());
}

/**
 * Demangle the type name of T.
 *
 * Usage:
 *   auto name = demangle<IndividualState>();
 */
template <typename T>
std::string demangle() {
  return demangle(typeid(T).name());
}

/**
 * Demangle the type name of an object instance.
 *
 * Usage:
 *   IndividualState s;
 *   auto name = demangle_instance(s);
 */
template <typename T>
std::string demangle_instance(const T& obj) {
  return demangle(typeid(obj).name());
}

}  // namespace common
}  // namespace cyber
}  // namespace world

#endif  // CYBER_COMMON_DEMANGLE_H_
