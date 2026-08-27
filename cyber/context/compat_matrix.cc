/******************************************************************************
 * compat_matrix.cc — CyberRT / World version compatibility implementation
 *
 * PRD #11: Version gate for individual execution context.
 *****************************************************************************/

#include "cyber/context/compat_matrix.h"

#include <charconv>
#include <fstream>
#include <sstream>

namespace world {
namespace cyber {
namespace context {

Version Version::Parse(std::string_view s) {
  Version v;
  size_t pos = 0;
  auto read_num = [&](uint32_t& out) {
    size_t dot = s.find('.', pos);
    if (dot == std::string_view::npos) dot = s.size();
    auto sub = s.substr(pos, dot - pos);
    std::from_chars(sub.data(), sub.data() + sub.size(), out);
    pos = dot + 1;
  };
  read_num(v.major);
  if (pos < s.size()) read_num(v.minor);
  if (pos < s.size()) read_num(v.patch);
  return v;
}

VersionRange CompatMatrix::CompileTimeRange() {
  // Default compatibility: world 1.x requires CyberRT 1.0.0 — 1.99.99
  return VersionRange{
      .min = {1, 0, 0},
      .max = {1, 99, 99},
      .migration_url = "https://world-docs/migration/default"};
}

CompatMatrix CompatMatrix::Load(const std::filesystem::path& yaml_path) {
  CompatMatrix mat;

  std::ifstream in(yaml_path);
  if (!in.is_open()) {
    // Fallback: single compile-time entry
    mat.entries_.push_back(Entry{
        .world_version_pattern = "*",
        .cyberrt_min = CompileTimeRange().min,
        .cyberrt_max = CompileTimeRange().max,
        .migration_url = CompileTimeRange().migration_url});
    return mat;
  }

  // Minimal YAML-like parsing (key: value per line)
  // Full YAML parser would be a dependency; this handles the simple schema.
  std::string line;
  Entry current;
  bool in_entry = false;
  while (std::getline(in, line)) {
    // Trim leading whitespace
    size_t start = line.find_first_not_of(" \t-");
    if (start == std::string::npos) continue;
    auto trimmed = line.substr(start);

    if (trimmed.find("world_version_pattern:") == 0) {
      if (in_entry) mat.entries_.push_back(current);
      current = Entry{};
      in_entry = true;
      auto val = trimmed.substr(trimmed.find(':') + 1);
      val.erase(0, val.find_first_not_of(" \t\""));
      val.erase(val.find_last_not_of(" \t\"") + 1);
      current.world_version_pattern = val;
    } else if (trimmed.find("cyberrt_min:") == 0) {
      auto val = trimmed.substr(trimmed.find(':') + 1);
      val.erase(0, val.find_first_not_of(" \t\""));
      val.erase(val.find_last_not_of(" \t\"") + 1);
      current.cyberrt_min = Version::Parse(val);
    } else if (trimmed.find("cyberrt_max:") == 0) {
      auto val = trimmed.substr(trimmed.find(':') + 1);
      val.erase(0, val.find_first_not_of(" \t\""));
      val.erase(val.find_last_not_of(" \t\"") + 1);
      current.cyberrt_max = Version::Parse(val);
    } else if (trimmed.find("migration_url:") == 0) {
      auto val = trimmed.substr(trimmed.find(':') + 1);
      val.erase(0, val.find_first_not_of(" \t\""));
      val.erase(val.find_last_not_of(" \t\"") + 1);
      current.migration_url = val;
    }
  }
  if (in_entry) mat.entries_.push_back(current);

  if (mat.entries_.empty()) {
    mat.entries_.push_back(Entry{
        .world_version_pattern = "*",
        .cyberrt_min = CompileTimeRange().min,
        .cyberrt_max = CompileTimeRange().max,
        .migration_url = CompileTimeRange().migration_url});
  }
  return mat;
}

VersionRange CompatMatrix::RequiredRangeFor(
    std::string_view world_version) const {
  // Simple pattern matching: "2.*" matches "2.x.y"
  for (const auto& entry : entries_) {
    const auto& pattern = entry.world_version_pattern;
    if (pattern == "*") {
      return VersionRange{entry.cyberrt_min, entry.cyberrt_max,
                          entry.migration_url};
    }
    // Check "N.*" pattern
    size_t star = pattern.find('*');
    if (star != std::string::npos) {
      auto prefix = pattern.substr(0, star);
      if (world_version.substr(0, prefix.size()) == prefix) {
        return VersionRange{entry.cyberrt_min, entry.cyberrt_max,
                            entry.migration_url};
      }
    }
    // Exact match
    if (pattern == world_version) {
      return VersionRange{entry.cyberrt_min, entry.cyberrt_max,
                          entry.migration_url};
    }
  }
  return CompileTimeRange();
}

}  // namespace context
}  // namespace cyber
}  // namespace world
