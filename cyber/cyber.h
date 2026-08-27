/******************************************************************************
 * cyber.h — Top-level world runtime facade
 *
 * Ported from: apollo/cyber/cyber.h
 * Namespace:   world::cyber
 *
 * Single include for common usage: Init, Node creation, Task, Time, Timer.
 *****************************************************************************/

#ifndef CYBER_CYBER_H_
#define CYBER_CYBER_H_

#include <memory>
#include <string>
#include <utility>

#include "cyber/common/log.h"
#include "cyber/component/component.h"
#include "cyber/init.h"
#include "cyber/node/node.h"
#include "cyber/task/task.h"
#include "cyber/time/time.h"
#include "cyber/timer/timer.h"

namespace world {
namespace cyber {

std::unique_ptr<Node> CreateNode(const std::string& node_name,
                                 const std::string& name_space = "");

}  // namespace cyber
}  // namespace world

#endif  // CYBER_CYBER_H_
