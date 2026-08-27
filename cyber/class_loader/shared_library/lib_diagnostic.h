/******************************************************************************
 * Copyright 2024 The World Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#ifndef CYBER_CLASS_LOADER_SHARED_LIBRARY_LIB_DIAGNOSTIC_H_
#define CYBER_CLASS_LOADER_SHARED_LIBRARY_LIB_DIAGNOSTIC_H_

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>

namespace world {
namespace cyber {
namespace class_loader {

/// Diagnostic information collected when dlopen fails.
struct LibDiagInfo {
  std::string ld_library_path;   ///< Current $LD_LIBRARY_PATH value.
  std::string ldconfig_matches;  ///< ldconfig -p lines matching the lib stem.
  std::string install_hint;      ///< Suggested package / env-var fix.
  std::string version_info;      ///< Already-installed versions of the lib.
};

/// Collects diagnostic context for a failed dlopen.
/// Called only on the error path — zero overhead when loading succeeds.
class LibDiagnostic {
 public:
  static LibDiagInfo collect(const std::string& lib_path) {
    LibDiagInfo info;

    // 1. LD_LIBRARY_PATH
    const char* ldpath = std::getenv("LD_LIBRARY_PATH");
    info.ld_library_path = ldpath ? std::string(ldpath) : "(not set)";

    // 2. Extract the library stem for ldconfig grep.
    //    e.g. "libcudnn.so.8" → "libcudnn"
    std::string stem = ExtractStem(lib_path);

    // 3. ldconfig matches
    info.ldconfig_matches = RunPipe("ldconfig -p 2>/dev/null | grep '" +
                                    stem + "' | head -10");
    if (info.ldconfig_matches.empty()) {
      info.ldconfig_matches = "(none found)";
    }

    // 4. Version info — look for any .so with the same stem
    info.version_info = RunPipe("find /usr/lib /usr/local/lib "
                                "/usr/lib/x86_64-linux-gnu 2>/dev/null "
                                "-name '" + stem + ".so*' | head -5");
    if (info.version_info.empty()) {
      info.version_info = "(no installed versions found)";
    }

    // 5. Install hint from built-in table
    info.install_hint = LookupInstallHint(stem);

    return info;
  }

  /// Format a full diagnostic message from base dlerror + collected info.
  static std::string Format(const std::string& base_err,
                            const LibDiagInfo& diag) {
    std::ostringstream os;
    os << base_err << "\n\n"
       << "=== Library Load Diagnostic ===\n"
       << "LD_LIBRARY_PATH: " << diag.ld_library_path << "\n"
       << "ldconfig matches:\n" << diag.ldconfig_matches << "\n"
       << "Installed versions:\n" << diag.version_info << "\n";
    if (!diag.install_hint.empty()) {
      os << "Suggested fix: " << diag.install_hint << "\n";
    }
    return os.str();
  }

 private:
  /// Extract library stem: "/usr/lib/libcudnn.so.8" → "libcudnn"
  static std::string ExtractStem(const std::string& path) {
    // Strip directory
    auto pos = path.rfind('/');
    std::string filename = (pos != std::string::npos) ? path.substr(pos + 1)
                                                      : path;
    // Strip .so* suffix
    pos = filename.find(".so");
    if (pos != std::string::npos) {
      return filename.substr(0, pos);
    }
    return filename;
  }

  /// Run a shell command and capture stdout (capped to 2048 bytes).
  static std::string RunPipe(const std::string& cmd) {
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return result;
    std::array<char, 256> buf;
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
      result += buf.data();
      if (result.size() > 2048) break;
    }
    pclose(pipe);
    // Trim trailing newline
    while (!result.empty() && result.back() == '\n') {
      result.pop_back();
    }
    return result;
  }

  /// Built-in hint table: lib stem → install suggestion.
  static std::string LookupInstallHint(const std::string& stem) {
    static const std::unordered_map<std::string, std::string> kHints = {
        {"libcudnn", "apt install libcudnn8 (or libcudnn9) — or set "
                     "LD_LIBRARY_PATH to your CUDA toolkit's lib64 directory"},
        {"libnvinfer", "Install TensorRT: apt install libnvinfer8 — or set "
                       "LD_LIBRARY_PATH=/usr/local/TensorRT/lib"},
        {"libcublas", "Install CUDA toolkit: apt install libcublas-dev — or "
                      "set LD_LIBRARY_PATH=/usr/local/cuda/lib64"},
        {"libcudart", "Install CUDA runtime: apt install nvidia-cuda-toolkit "
                      "— or set LD_LIBRARY_PATH=/usr/local/cuda/lib64"},
        {"libnccl", "apt install libnccl2 libnccl-dev"},
        {"libcufft", "apt install libcufft-dev"},
        {"libcurand", "apt install libcurand-dev"},
        {"libcusparse", "apt install libcusparse-dev"},
        {"libcusolver", "apt install libcusolver-dev"},
        {"libnvrtc", "Part of CUDA toolkit — apt install nvidia-cuda-toolkit"},
    };
    auto it = kHints.find(stem);
    return (it != kHints.end()) ? it->second : "";
  }
};

}  // namespace class_loader
}  // namespace cyber
}  // namespace world

#endif  // CYBER_CLASS_LOADER_SHARED_LIBRARY_LIB_DIAGNOSTIC_H_
