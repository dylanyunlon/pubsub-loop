#ifndef CYBER_PROFILER_FRAME_H_
#define CYBER_PROFILER_FRAME_H_

#include <list>
#include <stack>
#include <string>

#include "cyber/profiler/block.h"

namespace world {
namespace cyber {
namespace profiler {

class Frame {
 public:
  void Push(Block* block);
  Block* Top();
  void Pop();
  bool DumpToFile(const std::string& coroutine_name);
  void Clear();

  std::uint32_t size() const { return stack_.size(); }
  bool finished() const { return stack_.empty(); }

 private:
  std::stack<Block*> stack_;
  std::list<Block> storage_;
};

}  // namespace profiler
}  // namespace cyber
}  // namespace world

#endif  // CYBER_PROFILER_FRAME_H_
