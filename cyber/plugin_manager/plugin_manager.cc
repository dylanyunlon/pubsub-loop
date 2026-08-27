/******************************************************************************
 * plugin_manager.cc — Plugin discovery and loading
 *
 * Ported from: apollo/cyber/plugin_manager/plugin_manager.cc
 * Changes:
 *   - Namespace apollo → world
 *   - Environment variable prefix APOLLO_ → WORLD_
 *   - Removed tinyxml2 dependency (uses PluginDescription parsing)
 *****************************************************************************/

#include "cyber/plugin_manager/plugin_manager.h"

#include <dirent.h>

#include <memory>
#include <string>
#include <vector>

#include "cyber/common/environment.h"
#include "cyber/common/file.h"
#include "cyber/common/log.h"

namespace world {
namespace cyber {
namespace plugin_manager {

PluginManager::~PluginManager() {}

bool PluginManager::ProcessPluginDescriptionFile(const std::string& file_path,
                                                 std::string* library_path) {
  PluginDescription desc;
  if (!desc.ParseFromDescriptionFile(file_path)) {
    AWARN << "fail to process file " << file_path;
    return false;
  }
  *library_path = desc.library_path_;
  return true;
}

bool PluginManager::LoadPlugin(const std::string& plugin_description_file_path) {
  AINFO << "loading plugin from description[" << plugin_description_file_path
        << "]";

  auto description = std::make_shared<PluginDescription>();
  if (!description->ParseFromDescriptionFile(plugin_description_file_path)) {
    return false;
  }

  if (plugin_description_map_.find(description->name_) !=
      plugin_description_map_.end()) {
    AWARN << "plugin [" << description->name_ << "] already loaded";
    return true;
  }

  if (!LoadLibrary(description->actual_library_path_)) {
    AWARN << "plugin name[" << description->name_ << "] load failed, library["
          << description->actual_library_path_ << "] invalid";
    return false;
  }

  plugin_loaded_map_[description->name_] = true;
  plugin_description_map_[description->name_] = description;
  for (auto& class_pair : description->class_name_base_class_name_map_) {
    plugin_class_plugin_name_map_[class_pair] = description->name_;
  }
  AINFO << "plugin [" << description->name_ << "] load success";
  return true;
}

bool PluginManager::FindPluginIndexAndLoad(
    const std::string& plugin_index_path) {
  std::vector<std::string> plugin_index_list;
  common::FindPathByPattern(plugin_index_path, "", DT_REG, false,
                             &plugin_index_list);
  bool success = true;
  for (const auto& plugin_index : plugin_index_list) {
    std::string plugin_name = common::GetFileName(plugin_index);
    AINFO << "plugin index[" << plugin_index << "] name[" << plugin_name
          << "] found";
    if (plugin_description_map_.count(plugin_name)) {
      AWARN << "plugin [" << plugin_name << "] already loaded";
      continue;
    }

    auto description = std::make_shared<PluginDescription>(plugin_name);
    if (!description->ParseFromIndexFile(plugin_index)) {
      success = false;
      continue;
    }

    // Lazy load: register but don't load library yet
    plugin_loaded_map_[description->name_] = false;
    plugin_description_map_[description->name_] = description;
    for (auto& class_pair : description->class_name_base_class_name_map_) {
      plugin_class_plugin_name_map_[class_pair] = description->name_;
    }
    AINFO << "plugin [" << description->name_ << "] lazy registered";
  }
  return success;
}

bool PluginManager::LoadInstalledPlugins() {
  std::string plugin_index_path = common::GetEnv("WORLD_PLUGIN_INDEX_PATH");
  if (plugin_index_path.empty()) {
    const std::string dist_home = common::GetEnv("WORLD_DISTRIBUTION_HOME");
    plugin_index_path = dist_home + "/share/cyber_plugin_index";
  }
  AINFO << "loading plugins from WORLD_PLUGIN_INDEX_PATH[" << plugin_index_path
        << "]";

  // Support colon-separated paths
  size_t begin = 0;
  size_t index;
  do {
    index = plugin_index_path.find(':', begin);
    auto p = plugin_index_path.substr(begin, index - begin);
    if (common::DirectoryExists(p)) {
      AINFO << "loading plugins from path[" << p << "]";
      FindPluginIndexAndLoad(p);
    } else {
      AWARN << "plugin index path[" << p << "] not exists";
    }
    begin = index + 1;
  } while (index != std::string::npos);

  return true;
}

bool PluginManager::LoadLibrary(const std::string& library_path) {
  if (!class_loader_manager_.LoadLibrary(library_path)) {
    AWARN << "plugin library[" << library_path << "] load failed";
    return false;
  }
  return true;
}

PluginManager* PluginManager::Instance() { return instance_; }

PluginManager* PluginManager::instance_ = new PluginManager;

}  // namespace plugin_manager
}  // namespace cyber
}  // namespace world
