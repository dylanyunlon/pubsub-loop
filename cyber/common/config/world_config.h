/******************************************************************************
 * world_config.h — Single source of truth for world runtime configuration.
 *
 * Replaces per-module config structs (DistributedCoreConfig,
 * TensorParallelConfig, PipelineParallelConfig) that diverged on field
 * names, defaults, and validation.
 *
 * Lifecycle: init() once at startup, get() everywhere else.
 *
 * PRD: #494 Consolidate common configuration machinery
 *****************************************************************************/
#ifndef CYBER_COMMON_CONFIG_WORLD_CONFIG_H_
#define CYBER_COMMON_CONFIG_WORLD_CONFIG_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace world {
namespace config {

struct WorldConfig {
  // --- GPU topology ---------------------------------------------------------
  uint32_t num_gpus                = 1;
  uint32_t isg_size                = 32;  // ISG threads (warp size)

  // --- Memory budgets -------------------------------------------------------
  std::size_t smem_per_block_bytes = 48 * 1024;  // shared memory / block
  std::size_t global_pool_bytes    = 0;           // 0 = auto from device props

  // --- Streaming / pipelining -----------------------------------------------
  uint32_t num_streams             = 2;
  uint32_t pipeline_stages         = 2;

  // --- Transport defaults ---------------------------------------------------
  uint32_t shm_segment_size_mb     = 64;
  uint32_t rtps_domain_id          = 0;

  // --- Individual population limits -----------------------------------------
  uint32_t max_individuals         = 1'000'000;
  uint32_t max_channels            = 4096;

  // --- Tick timing ----------------------------------------------------------
  double   target_tick_hz          = 60.0;
  double   tick_budget_ms          = 16.0;

  /// Validate fields against hardware capabilities.
  /// Returns empty string on success, diagnostic on failure.
  std::string validate() const;
};

/// Initialise the process-global config.  Aborts on double-init.
void init(WorldConfig cfg);

/// Access the process-global config.  Aborts if init() hasn't been called.
const WorldConfig& get();

/// Test-only: reset so init() can be called again.
void reset_for_testing();

}  // namespace config
}  // namespace world

#endif  // CYBER_COMMON_CONFIG_WORLD_CONFIG_H_
