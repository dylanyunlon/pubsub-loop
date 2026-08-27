/******************************************************************************
 * compat_matrix.h — CyberRT / World version compatibility matrix
 *
 * PRD #11: Emit startup diagnostic when individual execution context
 *          detects incompatible Cyber RT middleware version.
 *
 * Namespace: world::cyber::context
 *****************************************************************************/

#ifndef CYBER_CONTEXT_COMPAT_MATRIX_H_
#define CYBER_CONTEXT_COMPAT_MATRIX_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace world {
namespace cyber {
namespace context {

struct Version {
  uint32_t major = 0;
  uint32_t minor = 0;
  uint32_t patch = 0;

  bool operator>=(const Version& o) const {
    if (major != o.major) return major > o.major;
    if (minor != o.minor) return minor > o.minor;
    return patch >= o.patch;
  }
  bool operator<=(const Version& o) const { return o >= *this; }

  std::string str() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." +
           std::to_string(patch);
  }

  static Version Parse(std::string_view s);
};

struct VersionRange {
  Version min;
  Version max;
  std::string migration_url;

  bool Contains(const Version& v) const { return v >= min && v <= max; }
  std::string min_str() const { return min.str(); }
  std::string max_str() const { return max.str(); }
};

class CompatMatrix {
 public:
  /// Load compatibility matrix from YAML config file.
  /// Falls back to compile-time defaults if file doesn't exist.
  static CompatMatrix Load(const std::filesystem::path& yaml_path);

  /// Look up the required CyberRT version range for a given world version.
  VersionRange RequiredRangeFor(std::string_view world_version) const;

  /// Compile-time fallback range.
  static VersionRange CompileTimeRange();

 private:
  struct Entry {
    std::string world_version_pattern;
    Version cyberrt_min;
    Version cyberrt_max;
    std::string migration_url;
  };
  std::vector<Entry> entries_;
};

}  // namespace context
}  // namespace cyber
}  // namespace world

#endif  // CYBER_CONTEXT_COMPAT_MATRIX_H_
