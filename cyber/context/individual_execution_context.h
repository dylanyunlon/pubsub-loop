/******************************************************************************
 * individual_execution_context.h — Per-individual execution context with
 *                                   CyberRT version gate
 *
 * PRD #11: Emit startup diagnostic when individual execution context
 *          detects incompatible Cyber RT middleware version.
 *
 * Namespace: world::cyber::context
 *****************************************************************************/

#ifndef CYBER_CONTEXT_INDIVIDUAL_EXECUTION_CONTEXT_H_
#define CYBER_CONTEXT_INDIVIDUAL_EXECUTION_CONTEXT_H_

#include <string>

#include "cyber/context/compat_matrix.h"
#include "cyber/context/context.h"
#include "cyber/diagnostic/diagnostic.h"

// World version — set by build system; fallback here.
#ifndef WORLD_VERSION_STRING
#define WORLD_VERSION_STRING "1.0.0"
#endif

namespace world {
namespace cyber {
namespace context {

/// Runtime CyberRT version query.
/// In production, this reads from the loaded _cyber_wrapper.so symbol table.
/// For now, uses compile-time constants that the build sets.
struct CyberRTVersion {
  static Version RuntimeVersion();
};

/// IndividualExecutionContext — initializes an individual's runtime
/// environment and gates on CyberRT version compatibility.
class IndividualExecutionContext {
 public:
  IndividualExecutionContext() = default;

  /// Initialize the execution context.
  /// Checks CyberRT version compatibility and emits a diagnostic if
  /// incompatible. Calls std::terminate() on version mismatch (Fatal).
  void Initialize();

  /// Initialize with an explicit compatibility matrix path.
  void Initialize(const std::filesystem::path& compat_yaml);

  /// Access the underlying context bag.
  Context& GetContext() { return ctx_; }
  const Context& GetContext() const { return ctx_; }

  bool IsInitialized() const { return initialized_; }

 private:
  Context ctx_;
  bool initialized_ = false;

  void VersionGate(const std::filesystem::path& compat_yaml);
};

// ─── Inline implementations ────────────────────────────────────────────────

inline Version CyberRTVersion::RuntimeVersion() {
  // Build-system overridable; defaults to compile-time version.
  // In production this would dlsym("cyber_runtime_version") from
  // _cyber_wrapper.so — but that FFI bridge is not yet built.
#ifdef CYBER_RT_VERSION_STRING
  return Version::Parse(CYBER_RT_VERSION_STRING);
#else
  return Version{1, 0, 0};
#endif
}

inline void IndividualExecutionContext::Initialize() {
  Initialize("config/cyberrt_compat_matrix.yaml");
}

inline void IndividualExecutionContext::Initialize(
    const std::filesystem::path& compat_yaml) {
  if (initialized_) return;
  VersionGate(compat_yaml);
  initialized_ = true;
}

inline void IndividualExecutionContext::VersionGate(
    const std::filesystem::path& compat_yaml) {
  const auto rt_ver = CyberRTVersion::RuntimeVersion();
  const auto compat = CompatMatrix::Load(compat_yaml);
  const auto req = compat.RequiredRangeFor(WORLD_VERSION_STRING);

  if (!req.Contains(rt_ver)) {
    WORLD_DIAGNOSTIC_ERROR(
        "Cyber RT version mismatch detected.\n"
        "  Runtime version : %s\n"
        "  Required range  : [%s, %s]\n"
        "  World version   : %s\n"
        "  Migration guide : %s\n"
        "IndividualExecutionContext will NOT initialize. "
        "No pub/sub channels will be created.",
        rt_ver.str().c_str(),
        req.min_str().c_str(),
        req.max_str().c_str(),
        WORLD_VERSION_STRING,
        req.migration_url.c_str());
    // WORLD_DIAGNOSTIC_ERROR calls std::terminate() — we never reach here.
  }

  AINFO << "Cyber RT " << rt_ver.str()
        << " verified compatible with world " << WORLD_VERSION_STRING;
}

}  // namespace context
}  // namespace cyber
}  // namespace world

#endif  // CYBER_CONTEXT_INDIVIDUAL_EXECUTION_CONTEXT_H_
