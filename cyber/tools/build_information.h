/******************************************************************************
 * build_information.h — Build metadata struct with safe defaults
 *
 * PRD #300: Add default member initializers so that BuildInformation{}
 *           or `BuildInformation bi;` never produces uninitialized fields.
 *           Existing aggregate initialization continues to work unchanged.
 *
 * Populated at build time by CMake-generated build_info.cc.
 * Queried at runtime by cyber_monitor, WorldInspector (Python), diagnostics.
 *
 * Namespace: world::cyber::tools
 *****************************************************************************/

#ifndef CYBER_TOOLS_BUILD_INFORMATION_H_
#define CYBER_TOOLS_BUILD_INFORMATION_H_

#include <cstdint>

namespace world {
namespace cyber {
namespace tools {

struct BuildInformation {
  const char* cuda_version        = "unknown";
  const char* compiler_version    = "unknown";
  const char* build_timestamp     = "unknown";
  const char* target_platform     = "unknown";
  const char* git_commit          = "unknown";
  bool        debug_build         = false;
  uint32_t    world_version_major = 0;
  uint32_t    world_version_minor = 0;
  uint32_t    world_version_patch = 0;
};

/// Returns the build-time populated BuildInformation.
/// Defined in CMake-generated build_info.cc.
const BuildInformation& GetBuildInfo();

}  // namespace tools
}  // namespace cyber
}  // namespace world

#endif  // CYBER_TOOLS_BUILD_INFORMATION_H_
