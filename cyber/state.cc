/******************************************************************************
 * state.cc — World runtime lifecycle state (atomic storage)
 *
 * Ported from: apollo/cyber/state.cc
 *****************************************************************************/

#include "cyber/state.h"

#include <atomic>

namespace world {
namespace cyber {

namespace {
std::atomic<State> g_cyber_state{STATE_UNINITIALIZED};
}

State GetState() { return g_cyber_state.load(std::memory_order_acquire); }

void SetState(const State& state) {
  g_cyber_state.store(state, std::memory_order_release);
}

}  // namespace cyber
}  // namespace world
