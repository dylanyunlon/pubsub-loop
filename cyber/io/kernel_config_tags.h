/******************************************************************************
 * kernel_config_tags.h — Tag types for per-field kernel config queries
 *
 * PRD #62: WorldExtension kernel_config tag-based query interface.
 *          Each tag maps to one field of KernelConfig via KernelConfigTraits.
 *
 * Namespace: world::cyber::io
 *****************************************************************************/

#ifndef CYBER_IO_KERNEL_CONFIG_TAGS_H_
#define CYBER_IO_KERNEL_CONFIG_TAGS_H_

#include <cstddef>
#include <cstdint>

namespace world {
namespace cyber {
namespace io {

// ── Tag types ───────────────────────────────────────────────────────────────
// Each tag is an empty struct used purely as a compile-time key.

struct BlockSizeTag {};
struct GridSizeTag {};
struct SharedMemSizeTag {};
struct StreamIndexTag {};
struct CooperativeLaunchTag {};
struct TensorCoreEnableTag {};
struct PriorityTag {};

// ── KernelConfig (full struct, backward-compatible) ─────────────────────────

struct KernelConfig {
  uint32_t block_size = 256;
  uint32_t grid_size = 1;
  size_t shared_mem_bytes = 0;
  int stream_index = 0;
  bool cooperative_launch = false;
  bool tensor_core_enable = false;
  int priority = 0;
};

// ── Traits: maps Tag → value_type + default ─────────────────────────────────

template <typename Tag>
struct KernelConfigTraits;

template <>
struct KernelConfigTraits<BlockSizeTag> {
  using value_type = uint32_t;
  static value_type default_value() { return 256; }
  static value_type extract(const KernelConfig& c) { return c.block_size; }
  static void inject(KernelConfig& c, value_type v) { c.block_size = v; }
};

template <>
struct KernelConfigTraits<GridSizeTag> {
  using value_type = uint32_t;
  static value_type default_value() { return 1; }
  static value_type extract(const KernelConfig& c) { return c.grid_size; }
  static void inject(KernelConfig& c, value_type v) { c.grid_size = v; }
};

template <>
struct KernelConfigTraits<SharedMemSizeTag> {
  using value_type = size_t;
  static value_type default_value() { return 0; }
  static value_type extract(const KernelConfig& c) {
    return c.shared_mem_bytes;
  }
  static void inject(KernelConfig& c, value_type v) {
    c.shared_mem_bytes = v;
  }
};

template <>
struct KernelConfigTraits<StreamIndexTag> {
  using value_type = int;
  static value_type default_value() { return 0; }
  static value_type extract(const KernelConfig& c) { return c.stream_index; }
  static void inject(KernelConfig& c, value_type v) { c.stream_index = v; }
};

template <>
struct KernelConfigTraits<CooperativeLaunchTag> {
  using value_type = bool;
  static value_type default_value() { return false; }
  static value_type extract(const KernelConfig& c) {
    return c.cooperative_launch;
  }
  static void inject(KernelConfig& c, value_type v) {
    c.cooperative_launch = v;
  }
};

template <>
struct KernelConfigTraits<TensorCoreEnableTag> {
  using value_type = bool;
  static value_type default_value() { return false; }
  static value_type extract(const KernelConfig& c) {
    return c.tensor_core_enable;
  }
  static void inject(KernelConfig& c, value_type v) {
    c.tensor_core_enable = v;
  }
};

template <>
struct KernelConfigTraits<PriorityTag> {
  using value_type = int;
  static value_type default_value() { return 0; }
  static value_type extract(const KernelConfig& c) { return c.priority; }
  static void inject(KernelConfig& c, value_type v) { c.priority = v; }
};

}  // namespace io
}  // namespace cyber
}  // namespace world

#endif  // CYBER_IO_KERNEL_CONFIG_TAGS_H_
