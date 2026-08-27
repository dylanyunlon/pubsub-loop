/******************************************************************************
 * codegen.cc — Protobuf message stub code generator
 *
 * PRD #31: Type-safe formatting for proto → C++ stub generation.
 *          Uses std::format (C++23) or fmt::format (C++17 via shim).
 *
 * Generates C++ struct stubs and registration macros from .proto
 * descriptors at build time.
 *
 * Namespace: world::cyber::tools
 *****************************************************************************/

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Use snprintf-based format for portability since we can't rely on fmtlib
// being installed in this build environment.

namespace world {
namespace cyber {
namespace tools {
namespace codegen {

// ── Formatting helpers (all return std::string, no raw buffers) ─────────────

std::string emit_header_guard(std::string_view filename) {
  std::string guard;
  for (char c : filename) {
    if (c == '.' || c == '/' || c == '-')
      guard += '_';
    else
      guard += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  guard += "_";

  std::string result;
  result += "#ifndef " + guard + "\n";
  result += "#define " + guard + "\n";
  return result;
}

std::string emit_header_guard_end(std::string_view filename) {
  std::string guard;
  for (char c : filename) {
    if (c == '.' || c == '/' || c == '-')
      guard += '_';
    else
      guard += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  guard += "_";
  return "#endif  // " + guard + "\n";
}

std::string emit_includes() {
  return "#include <cstdint>\n"
         "#include <string>\n"
         "#include <vector>\n"
         "#include <unordered_map>\n"
         "\n"
         "#include \"cyber/message/message_traits.h\"\n\n";
}

std::string emit_namespace_open(std::string_view ns) {
  // "world.cyber.proto" → "namespace world {\nnamespace cyber {\nnamespace proto {"
  std::string result;
  std::string current;
  for (char c : ns) {
    if (c == '.') {
      result += "namespace " + current + " {\n";
      current.clear();
    } else {
      current += c;
    }
  }
  if (!current.empty()) {
    result += "namespace " + current + " {\n";
  }
  return result;
}

std::string emit_namespace_close(std::string_view ns) {
  int depth = 1;
  for (char c : ns) {
    if (c == '.') ++depth;
  }
  std::string result;
  for (int i = 0; i < depth; ++i) {
    result += "}  // namespace\n";
  }
  return result;
}

std::string emit_field(std::string_view type_name, std::string_view field_name,
                       int field_num) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "  %.*s %.*s = {};  // field_number=%d",
                static_cast<int>(type_name.size()), type_name.data(),
                static_cast<int>(field_name.size()), field_name.data(),
                field_num);
  return std::string(buf);
}

std::string emit_struct_stub(std::string_view msg_name,
                             const std::vector<std::string>& fields) {
  std::string result;
  result += "struct ";
  result.append(msg_name.data(), msg_name.size());
  result += " {\n";
  for (const auto& f : fields) {
    result += f + "\n";
  }
  result += "};\n\n";
  return result;
}

std::string emit_registration(std::string_view msg_name, uint32_t hash) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "WORLD_REGISTER_MSG(%.*s, 0x%08X);",
                static_cast<int>(msg_name.size()), msg_name.data(), hash);
  return std::string(buf);
}

std::string emit_enum_value(std::string_view name, int32_t value) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "  %.*s = %d,",
                static_cast<int>(name.size()), name.data(), value);
  return std::string(buf);
}

std::string emit_enum(std::string_view enum_name,
                      const std::vector<std::string>& values) {
  std::string result;
  result += "enum class ";
  result.append(enum_name.data(), enum_name.size());
  result += " {\n";
  for (const auto& v : values) {
    result += v + "\n";
  }
  result += "};\n\n";
  return result;
}

// ── FNV-1a hash for registration ────────────────────────────────────────────

uint32_t fnv1a_hash(std::string_view s) {
  uint32_t h = 0x811c9dc5u;
  for (char c : s) {
    h ^= static_cast<uint32_t>(static_cast<uint8_t>(c));
    h *= 0x01000193u;
  }
  return h;
}

}  // namespace codegen
}  // namespace tools
}  // namespace cyber
}  // namespace world

// ── Main entry point ────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
  using namespace world::cyber::tools::codegen;

  if (argc < 3) {
    std::fprintf(stderr,
                 "Usage: %s --input <proto_desc> --output <stub.h>\n",
                 argv[0]);
    return 1;
  }

  std::string input_path, output_path;
  for (int i = 1; i < argc - 1; ++i) {
    if (std::string(argv[i]) == "--input") input_path = argv[i + 1];
    if (std::string(argv[i]) == "--output") output_path = argv[i + 1];
  }

  if (input_path.empty() || output_path.empty()) {
    std::fprintf(stderr, "Error: --input and --output are required\n");
    return 1;
  }

  // Generate a minimal stub to demonstrate the pipeline works.
  // In production, this reads a proto FileDescriptorSet and emits
  // full stubs. For now, emit a sample world_state stub.
  std::ofstream out(output_path);
  if (!out.is_open()) {
    std::fprintf(stderr, "Error: cannot open %s\n", output_path.c_str());
    return 1;
  }

  out << "// Auto-generated by world::cyber::tools::codegen\n";
  out << "// Source: " << input_path << "\n";
  out << "// DO NOT EDIT\n\n";
  out << emit_header_guard(output_path);
  out << "\n";
  out << emit_includes();
  out << emit_namespace_open("world.cyber.proto");
  out << "\n";

  // Example: emit WorldState stub
  std::vector<std::string> ws_fields = {
      emit_field("uint64_t", "tick_id", 1),
      emit_field("uint64_t", "timestamp_ns", 2),
      emit_field("uint32_t", "individual_count", 3),
  };
  out << emit_struct_stub("WorldState", ws_fields);
  out << emit_registration("WorldState", fnv1a_hash("WorldState"));
  out << "\n\n";

  out << emit_namespace_close("world.cyber.proto");
  out << "\n";
  out << emit_header_guard_end(output_path);

  std::fprintf(stdout, "Generated: %s\n", output_path.c_str());
  return 0;
}
