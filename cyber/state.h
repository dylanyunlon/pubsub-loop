/******************************************************************************
 * state.h — World runtime lifecycle state
 *
 * Ported from: apollo/cyber/state.h
 * Namespace:   world::cyber
 *
 * Global state enum used by all modules to check whether the world runtime
 * is initialized, shutting down, or fully stopped.
 *****************************************************************************/

#ifndef CYBER_STATE_H_
#define CYBER_STATE_H_

#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <thread>

#include "cyber/common/log.h"

namespace world {
namespace cyber {

enum State : std::uint8_t {
  STATE_UNINITIALIZED = 0,
  STATE_INITIALIZED,
  STATE_SHUTTING_DOWN,
  STATE_SHUTDOWN,
};

State GetState();
void SetState(const State& state);

inline bool OK() { return GetState() == STATE_INITIALIZED; }

inline bool IsShutdown() {
  return GetState() == STATE_SHUTTING_DOWN || GetState() == STATE_SHUTDOWN;
}

inline void WaitForShutdown() {
  while (!IsShutdown()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

inline void AsyncShutdown() {
  pid_t pid = getpid();
  if (kill(pid, SIGINT) != 0) {
    AERROR << strerror(errno);
  }
}

}  // namespace cyber
}  // namespace world

#endif  // CYBER_STATE_H_
