/******************************************************************************
 * build_info.cc — Default BuildInformation provider
 *
 * In production builds, CMake generates this file with actual values.
 * This default provides safe fallback values for tests and local builds.
 *****************************************************************************/

#include "cyber/tools/build_information.h"

namespace world {
namespace cyber {
namespace tools {

const BuildInformation& GetBuildInfo() {
  static const BuildInformation info{
      /* cuda_version     */ "N/A",
      /* compiler_version */ __VERSION__,
      /* build_timestamp  */ __DATE__ " " __TIME__,
      /* target_platform  */
#if defined(__x86_64__) || defined(_M_X64)
      "x86_64",
#elif defined(__aarch64__)
      "aarch64",
#else
      "unknown",
#endif
      /* git_commit       */ "dev",
      /* debug_build      */
#ifdef NDEBUG
      false,
#else
      true,
#endif
      /* world_version_major */ 1,
      /* world_version_minor */ 0,
      /* world_version_patch */ 0,
  };
  return info;
}

}  // namespace tools
}  // namespace cyber
}  // namespace world
