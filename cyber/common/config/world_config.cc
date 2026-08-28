#include "world_config.h"

#include <iostream>
#include <mutex>
#include <sstream>

namespace world {
namespace config {

namespace {
std::optional<WorldConfig> g_config;
std::mutex g_mu;
}  // namespace

std::string WorldConfig::validate() const {
  std::ostringstream err;

  if (isg_size == 0 || (isg_size & (isg_size - 1)) != 0) {
    err << "isg_size must be a power of 2, got " << isg_size << ". ";
  }
  if (smem_per_block_bytes == 0) {
    err << "smem_per_block_bytes must be > 0. ";
  }
  if (num_streams == 0) {
    err << "num_streams must be >= 1. ";
  }
  if (target_tick_hz <= 0.0) {
    err << "target_tick_hz must be positive. ";
  }
  if (max_individuals == 0) {
    err << "max_individuals must be > 0. ";
  }

  return err.str();
}

void init(WorldConfig cfg) {
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_config.has_value()) {
    std::cerr << "FATAL: world::config::init() called twice. Aborting.\n";
    std::abort();
  }
  auto diag = cfg.validate();
  if (!diag.empty()) {
    std::cerr << "FATAL: WorldConfig validation failed: " << diag << "\n";
    std::abort();
  }
  g_config.emplace(std::move(cfg));
}

const WorldConfig& get() {
  // Hot path: no lock after init completes (optional is immutable).
  if (!g_config.has_value()) {
    std::cerr << "FATAL: world::config::get() called before init(). "
                 "Ensure config is initialised during node startup.\n";
    std::abort();
  }
  return *g_config;
}

void reset_for_testing() {
  std::lock_guard<std::mutex> lk(g_mu);
  g_config.reset();
}

}  // namespace config
}  // namespace world
