/******************************************************************************
 * world_compat.h — Startup version-compatibility gate for individual nodes.
 *
 * Each module registers its compiled-against protocol version at static-init
 * time via WORLD_REGISTER_MODULE_VERSION.  When an individual node calls
 * WorldCompat::enforce_or_die() during init(), every registered module is
 * checked against the world instance's required version range.  Incompatible
 * nodes are blocked before they can publish/subscribe malformed state.
 *
 * PRD: #119 common::WorldCompat version compatibility checks
 *****************************************************************************/
#ifndef CYBER_COMMON_WORLD_COMPAT_H_
#define CYBER_COMMON_WORLD_COMPAT_H_

#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace world {
namespace common {

/// Semantic version triple.
struct ProtocolVersion {
  int major = 0;
  int minor = 0;
  int patch = 0;

  constexpr bool operator>=(const ProtocolVersion& r) const {
    if (major != r.major) return major > r.major;
    if (minor != r.minor) return minor > r.minor;
    return patch >= r.patch;
  }
  constexpr bool operator<=(const ProtocolVersion& r) const {
    return r >= *this;
  }
  constexpr bool operator==(const ProtocolVersion& r) const {
    return major == r.major && minor == r.minor && patch == r.patch;
  }
  constexpr bool operator!=(const ProtocolVersion& r) const {
    return !(*this == r);
  }

  std::string to_string() const {
    return std::to_string(major) + "." +
           std::to_string(minor) + "." +
           std::to_string(patch);
  }
};

/// Half-open version range [min, max].
struct VersionRange {
  ProtocolVersion min;
  ProtocolVersion max;

  bool contains(const ProtocolVersion& v) const {
    return v >= min && v <= max;
  }
  std::string to_string() const {
    return "[" + min.to_string() + ", " + max.to_string() + "]";
  }
};

/// Declaration a module emits at static-init time.
struct ModuleVersionDecl {
  std::string_view module_name;
  ProtocolVersion  compiled_version;
  VersionRange     compatible_range;
};

/// Result of a compatibility check.
struct CompatCheckResult {
  bool compatible = true;
  std::string diagnostic_message;
  std::vector<std::string> violations;
};

/// Process-global registry of module version declarations.
class WorldCompat {
 public:
  /// Register a module's version declaration.  Thread-safe.
  static void register_module(const ModuleVersionDecl& decl);

  /// Check all registered modules against @p world_required_range.
  static CompatCheckResult check_compatibility(
      const VersionRange& world_required_range);

  /// Snapshot of all registered modules.
  static std::vector<ModuleVersionDecl> registered_modules();

  /// Check + abort with full diagnostic if incompatible.
  /// Call this in IndividualNode::init().
  static void enforce_or_die(const VersionRange& world_required_range);

 private:
  static std::mutex& mu();
  static std::vector<ModuleVersionDecl>& decls();
};

/// Compile-time version constants.
constexpr ProtocolVersion kWorldProtocolVersion{3, 1, 0};
constexpr VersionRange kDefaultCompatibleRange{{3, 0, 0}, {3, 99, 99}};

}  // namespace common
}  // namespace world

/// Convenience macro for module authors.
/// Place in one .cc file per module:
///   WORLD_REGISTER_MODULE_VERSION("transport", 3, 1, 0);
#define WORLD_REGISTER_MODULE_VERSION(name, ma, mi, pa)                \
  namespace {                                                          \
  struct WorldCompat_##ma##_##mi##_##pa##_Reg {                        \
    WorldCompat_##ma##_##mi##_##pa##_Reg() {                           \
      ::world::common::WorldCompat::register_module(                   \
          {name, {ma, mi, pa}, ::world::common::kDefaultCompatibleRange}); \
    }                                                                  \
  } g_world_compat_##ma##_##mi##_##pa##_reg;                          \
  }

#endif  // CYBER_COMMON_WORLD_COMPAT_H_
