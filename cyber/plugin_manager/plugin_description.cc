/******************************************************************************
 * plugin_description.cc — Plugin description parsing
 *
 * Ported from: apollo/cyber/plugin_manager/plugin_description.cc
 * Changes:
 *   - Replaced tinyxml2 with simple regex-based XML attribute extraction
 *     (plugin descriptions are small, full XML parser not needed)
 *   - Environment variable paths use WORLD_PLUGIN_* prefix
 *****************************************************************************/

#include "cyber/plugin_manager/plugin_description.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "cyber/common/environment.h"
#include "cyber/common/file.h"
#include "cyber/common/log.h"

namespace world {
namespace cyber {
namespace plugin_manager {

namespace {

/// Extract attribute value from an XML-like tag string:
///   <library path="foo/bar.so"> → "foo/bar.so" for attr="path"
std::string ExtractAttr(const std::string& tag, const std::string& attr) {
  std::regex re(attr + R"(\s*=\s*"([^"]*)")");
  std::smatch m;
  if (std::regex_search(tag, m, re) && m.size() > 1) {
    return m[1].str();
  }
  return "";
}

}  // namespace

PluginDescription::PluginDescription() {}

PluginDescription::PluginDescription(const std::string& name) : name_(name) {}

PluginDescription::PluginDescription(
    const std::string& name, const std::string& description_index_path,
    const std::string& description_path,
    const std::string& actual_description_path,
    const std::string& library_path, const std::string& actual_library_path)
    : name_(name),
      description_index_path_(description_index_path),
      description_path_(description_path),
      actual_description_path_(actual_description_path),
      library_path_(library_path),
      actual_library_path_(actual_library_path) {}

bool PluginDescription::ParseFromIndexFile(const std::string& file_path) {
  description_index_path_ = file_path;
  name_ = common::GetFileName(file_path);

  if (!common::GetContent(file_path, &description_path_)) {
    AWARN << "plugin index[" << file_path << "] name[" << name_
          << "] invalid, read index file failed";
    return false;
  }
  return ParseFromDescriptionFile(description_path_);
}

bool PluginDescription::ParseFromDescriptionFile(const std::string& file_path) {
  if (description_path_.empty()) {
    description_path_ = file_path;
  }

  if (!common::GetFilePathWithEnv(description_path_,
                                   "WORLD_PLUGIN_DESCRIPTION_PATH",
                                   &actual_description_path_)) {
    AWARN << "plugin description[" << file_path << "] name[" << name_
          << "] invalid, description[" << description_path_
          << "] file not found";
    return false;
  }

  // Read the description XML file
  std::ifstream ifs(actual_description_path_);
  if (!ifs.is_open()) {
    AWARN << "Cannot open plugin description: " << actual_description_path_;
    return false;
  }
  std::ostringstream oss;
  oss << ifs.rdbuf();
  std::string content = oss.str();

  // Extract library path from root <library path="..."> tag
  std::regex lib_re(R"(<library\s+[^>]*path\s*=\s*"([^"]*)")");
  std::smatch lib_match;
  if (std::regex_search(content, lib_match, lib_re) && lib_match.size() > 1) {
    library_path_ = lib_match[1].str();
  }

  std::string plugin_name =
      std::regex_replace(library_path_, std::regex("/"), "__");
  if (name_.empty()) {
    name_ = plugin_name;
  }

  // Extract <class type="..." base_class="..."/> entries
  std::regex class_re(R"(<class\s+[^>]*>)");
  auto it = std::sregex_iterator(content.begin(), content.end(), class_re);
  auto end = std::sregex_iterator();
  for (; it != end; ++it) {
    std::string tag = (*it)[0].str();
    std::string class_name = ExtractAttr(tag, "type");
    std::string base_class = ExtractAttr(tag, "base_class");

    if (class_name.empty() || base_class.empty()) {
      AWARN << "plugin description[" << file_path << "] name[" << name_
            << "] invalid class entry";
      continue;
    }
    if (class_name_base_class_name_map_.count(class_name)) {
      AWARN << "plugin description[" << file_path << "] name[" << name_
            << "] duplicate class name[" << class_name << "]";
      continue;
    }
    class_name_base_class_name_map_[class_name] = base_class;
  }

  if (!common::GetFilePathWithEnv(library_path_, "WORLD_PLUGIN_LIB_PATH",
                                   &actual_library_path_)) {
    AWARN << "plugin description[" << file_path << "] name[" << name_
          << "] invalid, library[" << library_path_ << "] file not found";
    return false;
  }

  return true;
}

}  // namespace plugin_manager
}  // namespace cyber
}  // namespace world
