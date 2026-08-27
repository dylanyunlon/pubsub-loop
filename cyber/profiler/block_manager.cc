#include "cyber/profiler/block_manager.h"
#include "cyber/croutine/croutine.h"

namespace world {
namespace cyber {
namespace profiler {

thread_local std::unordered_map<std::string, Frame>
    BlockManager::routine_frame_map_{};

BlockManager::BlockManager() {}

void BlockManager::StartBlock(Block* block) {
  Frame* frame = GetRoutineFrame();
  if (!frame || !block) return;
  frame->Push(block);
  block->set_depth(frame->size());
  block->Start();
}

void BlockManager::EndBlock() {
  Frame* frame = GetRoutineFrame();
  if (!frame || frame->finished()) return;
  Block* block = frame->Top();
  block->End();
  frame->Pop();
  if (frame->finished()) {
    frame->DumpToFile(GetRoutineName());
    frame->Clear();
  }
}

std::string BlockManager::GetRoutineName() {
  std::string name("default_croutine");
  if (croutine::CRoutine::GetCurrentRoutine()) {
    name = croutine::CRoutine::GetCurrentRoutine()->name();
  }
  return name;
}

Frame* BlockManager::GetRoutineFrame() {
  return &routine_frame_map_[GetRoutineName()];
}

}  // namespace profiler
}  // namespace cyber
}  // namespace world
