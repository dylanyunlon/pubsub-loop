#include "cyber/profiler/frame.h"
#include <utility>
#include "cyber/common/log.h"

namespace world {
namespace cyber {
namespace profiler {

void Frame::Push(Block* block) {
  if (block) stack_.push(block);
}

Block* Frame::Top() {
  return stack_.empty() ? nullptr : stack_.top();
}

void Frame::Pop() {
  if (stack_.empty()) return;
  Block* p = stack_.top();
  stack_.pop();
  storage_.push_back(std::move(*p));
}

bool Frame::DumpToFile(const std::string& routine_name) {
  AINFO << "Frame : " << routine_name;
  for (const Block& block : storage_) {
    AINFO << block.depth() << "," << block.name() << "," << block.duration()
          << "," << block.begin_time_since_epoch()
          << "," << block.end_time_since_epoch();
  }
  return true;
}

void Frame::Clear() { storage_.clear(); }

}  // namespace profiler
}  // namespace cyber
}  // namespace world
