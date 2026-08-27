#ifndef CYBER_PROFILER_PROFILER_H_
#define CYBER_PROFILER_PROFILER_H_

#include "cyber/profiler/block.h"
#include "cyber/profiler/block_manager.h"

namespace world {
namespace cyber {
namespace profiler {

#if ENABLE_PROFILER

#if defined(__GNUC__) || defined(__clang__)
#define WFUNC __PRETTY_FUNCTION__
#else
#define WFUNC __func__
#endif

#define TOKEN_JOIN(x, y) x##y
#define UNIQUE_NAME(x) TOKEN_JOIN(perf_block_, x)

#define PERF_BLOCK(name, ...)                                       \
  world::cyber::profiler::Block UNIQUE_NAME(__LINE__)(name);        \
  world::cyber::profiler::BlockManager::Instance()->StartBlock(     \
      &UNIQUE_NAME(__LINE__));

#define PERF_BLOCK_END \
  world::cyber::profiler::BlockManager::Instance()->EndBlock();

#define PERF_FUNCTION(...) PERF_BLOCK(WFUNC, ##__VA_ARGS__)

#else

#define PERF_BLOCK(...)
#define PERF_BLOCK_END
#define PERF_FUNCTION(...)

#endif  // ENABLE_PROFILER

}  // namespace profiler
}  // namespace cyber
}  // namespace world

#endif  // CYBER_PROFILER_PROFILER_H_
