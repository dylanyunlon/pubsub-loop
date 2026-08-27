#include "cyber/profiler/block.h"
#include "cyber/profiler/block_manager.h"

namespace world {
namespace cyber {
namespace profiler {

Block::Block() {}
Block::Block(const std::string& name) : name_(name) {}
Block::~Block() {
  if (!finished()) BlockManager::Instance()->EndBlock();
}

void Block::Start() { begin_time_ = std::chrono::steady_clock::now(); }
void Block::End() { end_time_ = std::chrono::steady_clock::now(); }

std::uint64_t Block::begin_time_since_epoch() const {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             begin_time_.time_since_epoch()).count();
}
std::uint64_t Block::end_time_since_epoch() const {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             end_time_.time_since_epoch()).count();
}
std::uint64_t Block::duration() const {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             end_time_ - begin_time_).count();
}

}  // namespace profiler
}  // namespace cyber
}  // namespace world
