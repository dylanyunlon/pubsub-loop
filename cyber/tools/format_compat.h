/******************************************************************************
 * format_compat.h — C++17 / C++23 format compatibility shim
 *
 * PRD #31: Provides world::cyber::tools::format() that maps to:
 *   - std::format (C++23, __cpp_lib_format >= 202110L)
 *   - fmt::format (fmtlib >= 9, C++17 fallback)
 *   - snprintf-based fallback (no dependencies, limited)
 *
 * Namespace: world::cyber::tools
 *****************************************************************************/

#ifndef CYBER_TOOLS_FORMAT_COMPAT_H_
#define CYBER_TOOLS_FORMAT_COMPAT_H_

#if defined(__cpp_lib_format) && __cpp_lib_format >= 202110L
  // C++23 path — native std::format
  #include <format>
  namespace world { namespace cyber { namespace tools {
    using std::format;
  }}}

#elif __has_include(<fmt/core.h>)
  // fmtlib fallback for C++17
  #include <fmt/core.h>
  namespace world { namespace cyber { namespace tools {
    using fmt::format;
  }}}

#else
  // Minimal fallback — snprintf-based, only handles strings and integers
  #include <cstdarg>
  #include <cstdio>
  #include <string>
  #include <string_view>

  namespace world {
  namespace cyber {
  namespace tools {

  /// Minimal format fallback using snprintf.
  /// Supports %s-style format strings (not {} style).
  /// This is a last-resort; prefer installing fmtlib.
  template <typename... Args>
  std::string format(const char* fmt, Args&&... args) {
    // For the fallback, we use snprintf-style formatting.
    // Since std::format uses {} placeholders and snprintf uses %,
    // this won't be 1:1 compatible but covers basic codegen needs.
    char buf[4096];
    int n = std::snprintf(buf, sizeof(buf), fmt,
                          static_cast<Args&&>(args)...);
    if (n < 0) return fmt;
    if (static_cast<size_t>(n) >= sizeof(buf)) {
      std::string result(static_cast<size_t>(n) + 1, '\0');
      std::snprintf(result.data(), result.size(), fmt,
                    static_cast<Args&&>(args)...);
      result.resize(static_cast<size_t>(n));
      return result;
    }
    return std::string(buf, static_cast<size_t>(n));
  }

  }  // namespace tools
  }  // namespace cyber
  }  // namespace world

#endif

#endif  // CYBER_TOOLS_FORMAT_COMPAT_H_
