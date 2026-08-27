/******************************************************************************
 * cyber.cc — Top-level world runtime node creation
 *
 * Ported from: apollo/cyber/cyber.cc
 *****************************************************************************/

#include "cyber/cyber.h"

#include <memory>
#include <string>

#include "cyber/common/global_data.h"

namespace world {
namespace cyber {

std::unique_ptr<Node> CreateNode(const std::string& node_name,
                                 const std::string& name_space) {
  bool is_reality_mode = common::GlobalData::Instance()->IsRealityMode();
  if (is_reality_mode && !OK()) {
    AERROR << "please initialize world::cyber firstly.";
    return nullptr;
  }
  return std::unique_ptr<Node>(new Node(node_name, name_space));
}

}  // namespace cyber
}  // namespace world
