#ifndef CYBER_PROFILER_BLOCK_H_
#define CYBER_PROFILER_BLOCK_H_

#include <chrono>
#include <cstdint>
#include <string>

namespace world {
namespace cyber {
namespace profiler {

class Block {
 public:
  using time_point = std::chrono::time_point<std::chrono::steady_clock>;

  Block();
  explicit Block(const std::string& name);
  virtual ~Block();

  void Start();
  void End();

  const std::string& name() const { return name_; }
  std::uint32_t depth() const { return depth_; }
  void set_depth(std::uint32_t depth) { depth_ = depth; }

  const time_point& begin_time() const { return begin_time_; }
  const time_point& end_time() const { return end_time_; }

  std::uint64_t begin_time_since_epoch() const;
  std::uint64_t end_time_since_epoch() const;
  std::uint64_t duration() const;

  bool finished() const { return end_time_ > begin_time_; }

 private:
  std::string name_;
  std::uint32_t depth_{0};
  time_point begin_time_{};
  time_point end_time_{};
};

}  // namespace profiler
}  // namespace cyber
}  // namespace world

#endif  // CYBER_PROFILER_BLOCK_H_
