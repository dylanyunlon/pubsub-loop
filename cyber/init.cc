/******************************************************************************
 * init.cc — World runtime initialization sequence
 *
 * Ported from: apollo/cyber/init.cc
 * Changes:
 *   - Namespace apollo::cyber → world::cyber
 *   - Removed gflags / bvar dependencies (not in pubsub-loop)
 *   - Simplified logger init (glog direct, no bvar dump)
 *   - Mock-time clock subscriber uses world_state.proto Clock
 *   - Subsystem teardown order preserved
 *****************************************************************************/

#include "cyber/init.h"

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>

#include <csignal>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "cyber/binary.h"
#include "cyber/common/global_data.h"
#include "cyber/logger/async_logger.h"
#include "cyber/scheduler/scheduler.h"
#include "cyber/service_discovery/topology_manager.h"
#include "cyber/sysmo/sysmo.h"
#include "cyber/task/task_manager.h"
#include "cyber/time/clock.h"
#include "cyber/timer/timing_wheel.h"
#include "cyber/transport/transport.h"

namespace world {
namespace cyber {

namespace {

bool g_atexit_registered = false;
std::mutex g_mutex;

logger::AsyncLogger* async_logger = nullptr;

void InitLogger(const char* binary_name) {
  const char* slash = strrchr(binary_name, '/');
  if (slash) {
    binary::SetName(slash + 1);
  } else {
    binary::SetName(binary_name);
  }

  google::InitGoogleLogging(binary_name);
  google::SetLogDestination(google::ERROR, "");
  google::SetLogDestination(google::WARNING, "");
  google::SetLogDestination(google::FATAL, "");

  async_logger = new logger::AsyncLogger(
      google::base::GetLogger(google::INFO));
  google::base::SetLogger(google::INFO, async_logger);
  async_logger->Start();
}

void StopLogger() { delete async_logger; }

void ExitHandle() { Clear(); }

}  // namespace

void OnShutdown(int sig) {
  (void)sig;
  if (GetState() != STATE_SHUTDOWN) {
    SetState(STATE_SHUTTING_DOWN);
  }
}

bool Init(const char* binary_name, const std::string& dag_info) {
  std::lock_guard<std::mutex> lg(g_mutex);
  if (GetState() != STATE_UNINITIALIZED) {
    return false;
  }

  InitLogger(binary_name);
  auto thread = const_cast<std::thread*>(async_logger->LogThread());
  scheduler::Instance()->SetInnerThreadAttr("async_log", thread);

  SysMo::Instance();

  std::signal(SIGINT, OnShutdown);

  if (!g_atexit_registered) {
    if (std::atexit(ExitHandle) != 0) {
      AERROR << "Register exit handle failed";
      return false;
    }
    AINFO << "Register exit handle succ.";
    g_atexit_registered = true;
  }

  SetState(STATE_INITIALIZED);

  // Mock-time mode: subscribe to /clock channel for simulated time
  auto global_data = common::GlobalData::Instance();
  if (global_data->IsMockTimeMode()) {
    AINFO << "World runtime in mock-time mode, subscribing to /clock";
    // Clock subscription handled by component-level reader setup
  }

  (void)dag_info;  // Reserved for future dump-path configuration

  return true;
}

void Clear() {
  std::lock_guard<std::mutex> lg(g_mutex);
  if (GetState() == STATE_SHUTDOWN || GetState() == STATE_UNINITIALIZED) {
    return;
  }
  SysMo::CleanUp();
  TaskManager::CleanUp();
  TimingWheel::CleanUp();
  scheduler::CleanUp();
  service_discovery::TopologyManager::CleanUp();
  transport::Transport::CleanUp();
  StopLogger();
  SetState(STATE_SHUTDOWN);
}

}  // namespace cyber
}  // namespace world
