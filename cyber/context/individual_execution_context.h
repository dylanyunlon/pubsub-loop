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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

/// @brief Explicit declaration of execution-scope pub/sub channel ID range.
/// PRD #128: Must be configured before Initialize() to prevent
/// out-of-bounds channel handle creation in setup_transport().
struct ScopeConfig {
  uint32_t channel_id_begin = 0;       ///< Minimum allowed channel ID (inclusive)
  uint32_t channel_id_end = UINT32_MAX;///< Maximum allowed channel ID (exclusive)
  size_t   individual_capacity = 0;    ///< Max individuals in this scope (0 = unlimited)
};

/// IndividualExecutionContext — initializes an individual's runtime
/// environment and gates on CyberRT version compatibility.
///
/// PRD #128 FIX: initialize() now validates scope boundary BEFORE
/// setup_transport(), preventing out-of-bounds channel handle creation.
///
/// Correct order:
///   1. set_scope()                  — configure channel ID range
///   2. Initialize()                 — version gate + scope validate + transport + scheduler
///
/// Old (buggy) order was: setup_transport → setup_scheduler → validate_scope
class IndividualExecutionContext {
 public:
  IndividualExecutionContext() = default;

  /// @brief Configure scope boundary BEFORE calling Initialize().
  /// If not called, Initialize() uses a permissive default scope.
  void set_scope(const ScopeConfig& cfg) {
    scope_ = cfg;
    scope_configured_ = true;
  }

  const ScopeConfig& scope() const { return scope_; }

  /// Initialize the execution context.
  /// Order: version gate → validate_scope → setup_transport → setup_scheduler
  void Initialize();

  /// Initialize with an explicit compatibility matrix path.
  void Initialize(const std::filesystem::path& compat_yaml);

  /// Access the underlying context bag.
  Context& GetContext() { return ctx_; }
  const Context& GetContext() const { return ctx_; }

  bool IsInitialized() const { return initialized_; }

  /// Register an individual's channel IDs for scope validation.
  /// Must be called before Initialize() for each individual.
  void RegisterIndividualChannels(uint64_t individual_id,
                                  const std::vector<uint32_t>& channel_ids) {
    registered_channels_.push_back({individual_id, channel_ids});
  }

 private:
  Context ctx_;
  bool initialized_ = false;
  ScopeConfig scope_;
  bool scope_configured_ = false;

  struct IndividualChannelSet {
    uint64_t individual_id;
    std::vector<uint32_t> channel_ids;
  };
  std::vector<IndividualChannelSet> registered_channels_;

  void VersionGate(const std::filesystem::path& compat_yaml);
  void ValidateScopeBoundary();
  void SetupTransport();
  void SetupScheduler();
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

  // PRD #128 FIX: Correct initialization order.
  // validate_scope BEFORE setup_transport to prevent OOB channel handles.
  VersionGate(compat_yaml);
  ValidateScopeBoundary();  // Step 1: verify all channel IDs in [begin, end)
  SetupTransport();         // Step 2: safe to create ChannelWriter/Reader now
  SetupScheduler();         // Step 3: scheduler can reference valid channels

  initialized_ = true;
}

inline void IndividualExecutionContext::ValidateScopeBoundary() {
  for (const auto& entry : registered_channels_) {
    for (uint32_t cid : entry.channel_ids) {
      if (cid < scope_.channel_id_begin || cid >= scope_.channel_id_end) {
        WORLD_DIAGNOSTIC_ERROR(
            "Individual %" PRIu64 " has channel ID %u outside valid scope "
            "[%u, %u). Refusing to initialize transport layer.\n"
            "Call set_scope() with a wider range or remove the "
            "out-of-bounds channel registration.",
            entry.individual_id, cid,
            scope_.channel_id_begin, scope_.channel_id_end);
        // WORLD_DIAGNOSTIC_ERROR calls std::terminate()
      }
    }
  }

  if (scope_.individual_capacity > 0 &&
      registered_channels_.size() > scope_.individual_capacity) {
    WORLD_DIAGNOSTIC_ERROR(
        "Registered individuals (%zu) exceed scope capacity (%zu).",
        registered_channels_.size(), scope_.individual_capacity);
  }

  AINFO << "Scope boundary validated: " << registered_channels_.size()
        << " individuals, channel range [" << scope_.channel_id_begin
        << ", " << scope_.channel_id_end << ")";
}

inline void IndividualExecutionContext::SetupTransport() {
  // Create ChannelWriter<T>/ChannelReader<T> handles for all registered
  // individuals. Safe to call because ValidateScopeBoundary() passed.
  // Actual transport setup delegates to the Transport singleton.
  AINFO << "Setting up transport for " << registered_channels_.size()
        << " individuals";
  // TODO: integrate with cyber/transport/transport.h
}

inline void IndividualExecutionContext::SetupScheduler() {
  // Initialize per-individual CRoutine scheduling.
  AINFO << "Setting up scheduler";
  // TODO: integrate with cyber/scheduler/scheduler.h
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
