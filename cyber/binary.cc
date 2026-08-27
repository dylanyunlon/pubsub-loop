/******************************************************************************
 * binary.cc — World process binary name (thread-safe storage)
 *
 * Ported from: apollo/cyber/binary.cc
 *****************************************************************************/

#include "cyber/binary.h"

#include <mutex>

namespace {
std::mutex g_binary_mu;
std::string g_binary_name;  // NOLINT
}  // namespace

namespace world {
namespace cyber {
namespace binary {

std::string GetName() {
  std::lock_guard<std::mutex> lock(g_binary_mu);
  return g_binary_name;
}

void SetName(const std::string& name) {
  std::lock_guard<std::mutex> lock(g_binary_mu);
  g_binary_name = name;
}

}  // namespace binary
}  // namespace cyber
}  // namespace world
