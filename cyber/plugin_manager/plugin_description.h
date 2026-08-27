/******************************************************************************
 * plugin_description.h — Plugin description metadata
 *
 * Ported from: apollo/cyber/plugin_manager/plugin_description.h
 * Namespace:   world::cyber::plugin_manager
 *****************************************************************************/

#pragma once

#include <map>
#include <string>

namespace world {
namespace cyber {
namespace plugin_manager {

class PluginDescription {
 public:
  std::string name_;
  std::string description_index_path_;
  std::string description_path_;
  std::string actual_description_path_;
  std::string library_path_;
  std::string actual_library_path_;

  std::map<std::string, std::string> class_name_base_class_name_map_;

  PluginDescription();
  explicit PluginDescription(const std::string& name);
  PluginDescription(const std::string& name,
                    const std::string& description_index_path,
                    const std::string& description_path,
                    const std::string& actual_description_path,
                    const std::string& library_path,
                    const std::string& actual_library_path);

  bool ParseFromIndexFile(const std::string& file_path);
  bool ParseFromDescriptionFile(const std::string& file_path);
};

}  // namespace plugin_manager
}  // namespace cyber
}  // namespace world
