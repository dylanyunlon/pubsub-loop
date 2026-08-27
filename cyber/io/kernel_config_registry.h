/******************************************************************************
 * kernel_config_registry.h — Per-individual kernel config storage + tag query
 *
 * PRD #62: KernelConfigRegistry stores full KernelConfig per individual.
 *          Tag-based get<Tag>(id) extracts a single field without copying
 *          the entire struct.
 *
 * Namespace: world::cyber::io
 *****************************************************************************/

#ifndef CYBER_IO_KERNEL_CONFIG_REGISTRY_H_
#define CYBER_IO_KERNEL_CONFIG_REGISTRY_H_

#include <mutex>
#include <string>
#include <unordered_map>

#include "cyber/io/kernel_config_tags.h"

namespace world {
namespace cyber {
namespace io {

using IndividualId = std::string;

class KernelConfigRegistry {
 public:
  /// Register or update the full KernelConfig for an individual.
  void Set(const IndividualId& id, const KernelConfig& config) {
    std::lock_guard<std::mutex> lock(mu_);
    configs_[id] = config;
  }

  /// Set a single tagged field for an individual.
  template <typename Tag>
  void Set(const IndividualId& id,
           typename KernelConfigTraits<Tag>::value_type value) {
    std::lock_guard<std::mutex> lock(mu_);
    KernelConfigTraits<Tag>::inject(configs_[id], value);
  }

  /// Get full config (backward-compatible path).
  KernelConfig GetFull(const IndividualId& id) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = configs_.find(id);
    if (it != configs_.end()) return it->second;
    return KernelConfig{};
  }

  /// Get a single tagged field — no full-struct copy.
  template <typename Tag>
  typename KernelConfigTraits<Tag>::value_type Get(
      const IndividualId& id) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = configs_.find(id);
    if (it != configs_.end()) {
      return KernelConfigTraits<Tag>::extract(it->second);
    }
    return KernelConfigTraits<Tag>::default_value();
  }

  bool Has(const IndividualId& id) const {
    std::lock_guard<std::mutex> lock(mu_);
    return configs_.count(id) > 0;
  }

  void Remove(const IndividualId& id) {
    std::lock_guard<std::mutex> lock(mu_);
    configs_.erase(id);
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mu_);
    configs_.clear();
  }

 private:
  mutable std::mutex mu_;
  std::unordered_map<IndividualId, KernelConfig> configs_;
};

}  // namespace io
}  // namespace cyber
}  // namespace world

#endif  // CYBER_IO_KERNEL_CONFIG_REGISTRY_H_
