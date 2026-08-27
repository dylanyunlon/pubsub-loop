/******************************************************************************
 * diagnostic.h — Startup diagnostic output for world runtime errors
 *
 * PRD #11: Emit startup diagnostic when individual execution context
 *          detects incompatible Cyber RT middleware version.
 *
 * Writes to BOTH stderr (immediate, visible in terminal) AND log file
 * (persistent, via existing AERROR/AFATAL infrastructure).
 *
 * Namespace: world::cyber::diagnostic
 *****************************************************************************/

#ifndef CYBER_DIAGNOSTIC_DIAGNOSTIC_H_
#define CYBER_DIAGNOSTIC_DIAGNOSTIC_H_

#include <cstdio>
#include <cstdlib>
#include <string>

#include "cyber/common/log.h"

namespace world {
namespace cyber {
namespace diagnostic {

enum class DiagLevel { Info, Warn, Error, Fatal };

inline const char* DiagLevelStr(DiagLevel level) {
  switch (level) {
    case DiagLevel::Info:  return "INFO";
    case DiagLevel::Warn:  return "WARN";
    case DiagLevel::Error: return "ERROR";
    case DiagLevel::Fatal: return "FATAL";
  }
  return "UNKNOWN";
}

/// Emit diagnostic to stderr AND log infrastructure.
/// Fatal level calls std::terminate() after output.
inline void EmitDiagnostic(DiagLevel level, const std::string& msg,
                           const char* file, int line) {
  // stderr — immediate, always visible
  std::fprintf(stderr, "[WORLD %s] [%s:%d]\n%s\n",
               DiagLevelStr(level), file, line, msg.c_str());
  std::fflush(stderr);

  // Log infrastructure — persistent
  switch (level) {
    case DiagLevel::Info:
      AINFO << "[DIAGNOSTIC] " << msg;
      break;
    case DiagLevel::Warn:
      AWARN << "[DIAGNOSTIC] " << msg;
      break;
    case DiagLevel::Error:
      AERROR << "[DIAGNOSTIC] " << msg;
      break;
    case DiagLevel::Fatal:
      AFATAL << "[DIAGNOSTIC] " << msg;
      std::terminate();
      break;
  }
}

/// Simple string formatting helper (variadic, snprintf-based).
template <typename... Args>
std::string Format(const char* fmt, Args... args) {
  // First pass: measure
  int n = std::snprintf(nullptr, 0, fmt, args...);
  if (n <= 0) return fmt;
  std::string buf(static_cast<size_t>(n) + 1, '\0');
  std::snprintf(buf.data(), buf.size(), fmt, args...);
  buf.resize(static_cast<size_t>(n));
  return buf;
}

}  // namespace diagnostic
}  // namespace cyber
}  // namespace world

// Convenience macros — these capture __FILE__ and __LINE__ at call site.

#define WORLD_DIAGNOSTIC_INFO(fmt, ...)                                   \
  do {                                                                    \
    auto _msg = ::world::cyber::diagnostic::Format(fmt, ##__VA_ARGS__);  \
    ::world::cyber::diagnostic::EmitDiagnostic(                           \
        ::world::cyber::diagnostic::DiagLevel::Info,                      \
        _msg, __FILE__, __LINE__);                                        \
  } while (0)

#define WORLD_DIAGNOSTIC_WARN(fmt, ...)                                   \
  do {                                                                    \
    auto _msg = ::world::cyber::diagnostic::Format(fmt, ##__VA_ARGS__);  \
    ::world::cyber::diagnostic::EmitDiagnostic(                           \
        ::world::cyber::diagnostic::DiagLevel::Warn,                      \
        _msg, __FILE__, __LINE__);                                        \
  } while (0)

#define WORLD_DIAGNOSTIC_ERROR(fmt, ...)                                  \
  do {                                                                    \
    auto _msg = ::world::cyber::diagnostic::Format(fmt, ##__VA_ARGS__);  \
    ::world::cyber::diagnostic::EmitDiagnostic(                           \
        ::world::cyber::diagnostic::DiagLevel::Fatal,                     \
        _msg, __FILE__, __LINE__);                                        \
  } while (0)

#endif  // CYBER_DIAGNOSTIC_DIAGNOSTIC_H_
