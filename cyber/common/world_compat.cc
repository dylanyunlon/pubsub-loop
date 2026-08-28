#include "world_compat.h"

#include <iostream>
#include <sstream>

namespace world {
namespace common {

std::mutex& WorldCompat::mu() {
  static std::mutex m;
  return m;
}

std::vector<ModuleVersionDecl>& WorldCompat::decls() {
  static std::vector<ModuleVersionDecl> d;
  return d;
}

void WorldCompat::register_module(const ModuleVersionDecl& decl) {
  std::lock_guard<std::mutex> lock(mu());
  decls().push_back(decl);
}

CompatCheckResult WorldCompat::check_compatibility(
    const VersionRange& world_required_range) {
  std::lock_guard<std::mutex> lock(mu());
  CompatCheckResult result;

  for (const auto& m : decls()) {
    // Check 1: module's compiled version falls within world's range.
    if (!world_required_range.contains(m.compiled_version)) {
      result.compatible = false;
      std::string v;
      v += "Module '";
      v += m.module_name;
      v += "' compiled against protocol ";
      v += m.compiled_version.to_string();
      v += " which is outside world-required range ";
      v += world_required_range.to_string();
      result.violations.push_back(std::move(v));
    }

    // Check 2: world's range overlaps module's own compatible range.
    bool overlap =
        world_required_range.min <= m.compatible_range.max &&
        m.compatible_range.min <= world_required_range.max;
    if (!overlap) {
      result.compatible = false;
      std::string v;
      v += "Module '";
      v += m.module_name;
      v += "' compatible range ";
      v += m.compatible_range.to_string();
      v += " does not overlap world-required range ";
      v += world_required_range.to_string();
      result.violations.push_back(std::move(v));
    }
  }

  if (!result.compatible) {
    std::ostringstream oss;
    oss << "WorldCompat: " << result.violations.size()
        << " version violation(s) detected at node startup.\n";
    for (const auto& v : result.violations) {
      oss << "  - " << v << "\n";
    }
    result.diagnostic_message = oss.str();
  }
  return result;
}

std::vector<ModuleVersionDecl> WorldCompat::registered_modules() {
  std::lock_guard<std::mutex> lock(mu());
  return decls();
}

void WorldCompat::enforce_or_die(const VersionRange& world_required_range) {
  auto result = check_compatibility(world_required_range);
  if (!result.compatible) {
    std::cerr << "\n=== FATAL: World Protocol Version Incompatibility ===\n"
              << result.diagnostic_message
              << "Node cannot join pub/sub network. Aborting.\n"
              << std::endl;
    std::abort();
  }
}

}  // namespace common
}  // namespace world
