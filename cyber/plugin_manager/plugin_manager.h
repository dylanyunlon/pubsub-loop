/******************************************************************************
 * plugin_manager.h — Dynamic individual-type plugin registry
 *
 * Ported from: apollo/cyber/plugin_manager/plugin_manager.h
 * Namespace:   world::cyber::plugin_manager
 *
 * Manages discovery and loading of individual-type shared libraries.
 * Supports lazy loading: descriptions are parsed at startup, but .so files
 * are loaded on first CreateInstance call.
 *****************************************************************************/

#pragma once

#include <cxxabi.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cyber/class_loader/class_loader_manager.h"
#include "cyber/common/log.h"
#include "cyber/plugin_manager/plugin_description.h"

namespace world {
namespace cyber {
namespace plugin_manager {

class PluginManager {
 public:
  ~PluginManager();

  bool ProcessPluginDescriptionFile(const std::string& file_path,
                                    std::string* library_path);
  bool LoadPlugin(const std::string& plugin_description_file_path);
  bool FindPluginIndexAndLoad(const std::string& plugin_index_path);
  bool LoadInstalledPlugins();
  bool LoadLibrary(const std::string& library_path);

  static PluginManager* Instance();

  /// Create an instance of a derived plugin class.
  template <typename Base>
  std::shared_ptr<Base> CreateInstance(const std::string& derived_class) {
    AINFO << "creating plugin instance of " << derived_class;
    if (!CheckAndLoadPluginLibrary<Base>(derived_class)) {
      AERROR << "plugin of class " << derived_class << " have not been loaded";
      return nullptr;
    }
    return class_loader_manager_.CreateClassObj<Base>(derived_class);
  }

  /// Get the home directory path of the plugin containing class_name.
  template <typename Base>
  std::string GetPluginClassHomePath(const std::string& class_name) {
    if (!CheckAndLoadPluginLibrary<Base>(class_name)) {
      AERROR << "plugin of class " << class_name << " have not been loaded";
      return "";
    }
    std::string library_path =
        class_loader_manager_.GetClassValidLibrary<Base>(class_name);
    if (library_path.empty()) {
      AWARN << "plugin of class " << class_name << " not found";
      return "";
    }
    for (auto& [name, desc] : plugin_description_map_) {
      if (desc->actual_library_path_ == library_path) {
        std::string relative_prefix = "share/";
        std::string home = common::GetDirName(desc->description_path_);
        if (home.rfind(relative_prefix, 0) == 0) {
          home = home.substr(relative_prefix.size());
        }
        return home;
      }
    }
    return "";
  }

  /// Get configuration file path for a plugin class.
  template <typename Base>
  std::string GetPluginConfPath(const std::string& class_name,
                                const std::string& conf_name) {
    std::string plugin_home = GetPluginClassHomePath<Base>(class_name);
    std::string relative_conf = plugin_home + "/" + conf_name;
    std::string actual_conf;
    if (common::GetFilePathWithEnv(relative_conf, "WORLD_CONF_PATH",
                                    &actual_conf)) {
      return actual_conf;
    }
    return relative_conf;
  }

  /// Check if a plugin's library is loaded.
  template <typename Base>
  bool IsLibraryLoaded(const std::string& class_name) {
    int status = 0;
    char* demangled = abi::__cxa_demangle(typeid(Base).name(), 0, 0, &status);
    std::string base_class_name = (demangled && status == 0) ? demangled : "";
    free(demangled);

    auto key = std::make_pair(class_name, base_class_name);
    if (plugin_class_plugin_name_map_.find(key) ==
        plugin_class_plugin_name_map_.end()) {
      return false;
    }
    std::string plugin_name = plugin_class_plugin_name_map_[key];
    auto it = plugin_loaded_map_.find(plugin_name);
    return it != plugin_loaded_map_.end() && it->second;
  }

  /// Ensure a plugin's library is loaded, loading lazily if needed.
  template <typename Base>
  bool CheckAndLoadPluginLibrary(const std::string& class_name) {
    if (IsLibraryLoaded<Base>(class_name)) return true;

    int status = 0;
    char* demangled = abi::__cxa_demangle(typeid(Base).name(), 0, 0, &status);
    std::string base_class_name = (demangled && status == 0) ? demangled : "";
    free(demangled);

    auto key = std::make_pair(class_name, base_class_name);
    if (plugin_class_plugin_name_map_.find(key) ==
        plugin_class_plugin_name_map_.end()) {
      AWARN << "plugin of class " << class_name
            << " not found, check registration";
      return false;
    }
    std::string plugin_name = plugin_class_plugin_name_map_[key];
    auto desc_it = plugin_description_map_.find(plugin_name);
    if (desc_it == plugin_description_map_.end()) {
      AWARN << "plugin description of class " << class_name << " not found";
      return false;
    }
    return LoadLibrary(desc_it->second->actual_library_path_);
  }

  /// Get all derived class names registered for a base type.
  template <typename Base>
  std::vector<std::string> GetDerivedClassNameByBaseClass() {
    int status = 0;
    char* demangled = abi::__cxa_demangle(typeid(Base).name(), 0, 0, &status);
    std::string base_class_name = (demangled && status == 0) ? demangled : "";
    free(demangled);

    std::vector<std::string> result;
    for (auto& [key, _] : plugin_class_plugin_name_map_) {
      if (key.second == base_class_name) {
        result.push_back(key.first);
      }
    }
    return result;
  }

 private:
  class_loader::ClassLoaderManager class_loader_manager_;
  std::map<std::string, std::shared_ptr<PluginDescription>>
      plugin_description_map_;
  std::map<std::string, bool> plugin_loaded_map_;
  std::map<std::pair<std::string, std::string>, std::string>
      plugin_class_plugin_name_map_;

  static PluginManager* instance_;
};

#define CYBER_PLUGIN_MANAGER_REGISTER_PLUGIN(name, base) \
  CLASS_LOADER_REGISTER_CLASS(name, base)

}  // namespace plugin_manager
}  // namespace cyber
}  // namespace world
