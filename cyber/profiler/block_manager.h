#ifndef CYBER_PROFILER_BLOCK_MANAGER_H_
#define CYBER_PROFILER_BLOCK_MANAGER_H_

#include <string>
#include <unordered_map>

#include "cyber/common/macros.h"
#include "cyber/profiler/block.h"
#include "cyber/profiler/frame.h"

namespace world {
namespace cyber {
namespace profiler {

class BlockManager {
 public:
  using RoutineName = std::string;
  using RoutineFrameMap = std::unordered_map<RoutineName, Frame>;

  void StartBlock(Block* block);
  void EndBlock();

 private:
  std::string GetRoutineName();
  Frame* GetRoutineFrame();

  static thread_local RoutineFrameMap routine_frame_map_;
  DECLARE_SINGLETON(BlockManager)
};

}  // namespace profiler
}  // namespace cyber
}  // namespace world

#endif  // CYBER_PROFILER_BLOCK_MANAGER_H_
