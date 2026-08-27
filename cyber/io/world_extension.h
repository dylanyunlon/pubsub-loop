/******************************************************************************
 * world_extension.h — World extension interface with per-individual
 *                      kernel config queries
 *
 * PRD #62: Add query_kernel_config<Tag>(tag, id) to WorldExtension.
 *          Preserves existing kernel_config(id) for backward compatibility.
 *
 * Namespace: world::cyber::io
 *****************************************************************************/

#ifndef CYBER_IO_WORLD_EXTENSION_H_
#define CYBER_IO_WORLD_EXTENSION_H_

#include <string>

#include "cyber/io/kernel_config_registry.h"
#include "cyber/io/kernel_config_tags.h"

namespace world {
namespace cyber {
namespace io {

class WorldExtension {
 public:
  /// EXISTING — full-struct return, preserved for backward compatibility.
  KernelConfig kernel_config(const IndividualId& id) const {
    return registry_.GetFull(id);
  }

  /// NEW (PRD #62) — tag-based per-field query, no full-struct copy.
  /// Falls back to KernelConfigTraits<Tag>::default_value() if id not
  /// registered.
  template <typename Tag>
  typename KernelConfigTraits<Tag>::value_type query_kernel_config(
      Tag /*tag*/, const IndividualId& id) const {
    return registry_.Get<Tag>(id);
  }

  /// Register or update kernel config for an individual.
  void set_kernel_config(const IndividualId& id, const KernelConfig& config) {
    registry_.Set(id, config);
  }

  /// Set a single tagged field for an individual.
  template <typename Tag>
  void set_kernel_config(Tag /*tag*/, const IndividualId& id,
                         typename KernelConfigTraits<Tag>::value_type value) {
    registry_.template Set<Tag>(id, value);
  }

  KernelConfigRegistry& registry() { return registry_; }
  const KernelConfigRegistry& registry() const { return registry_; }

 private:
  KernelConfigRegistry registry_;
};

}  // namespace io
}  // namespace cyber
}  // namespace world

#endif  // CYBER_IO_WORLD_EXTENSION_H_
