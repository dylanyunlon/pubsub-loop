/******************************************************************************
 * init.h — World runtime initialization and shutdown
 *
 * Ported from: apollo/cyber/init.h
 * Namespace:   world::cyber
 *
 * Call Init() once at process start to bootstrap the world runtime:
 *   - Logger, scheduler, transport, service discovery, timing wheel
 * Call Clear() or let atexit handle teardown.
 *****************************************************************************/

#ifndef CYBER_INIT_H_
#define CYBER_INIT_H_

#include <string>

#include "cyber/common/log.h"
#include "cyber/state.h"

namespace world {
namespace cyber {

bool Init(const char* binary_name, const std::string& dag_info = "");
void Clear();
void OnShutdown(int sig);

}  // namespace cyber
}  // namespace world

#endif  // CYBER_INIT_H_
