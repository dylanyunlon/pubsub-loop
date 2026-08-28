/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#ifndef CYBER_DATA_CHANNEL_BUFFER_H_
#define CYBER_DATA_CHANNEL_BUFFER_H_

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "cyber/common/global_data.h"
#include "cyber/common/log.h"
#include "cyber/data/data_notifier.h"

namespace world {
namespace cyber {
namespace data {

using world::cyber::common::GlobalData;

template <typename T>
class ChannelBuffer {
 public:
  using BufferType = CacheBuffer<std::shared_ptr<T>>;
  ChannelBuffer(uint64_t channel_id, BufferType* buffer)
      : channel_id_(channel_id), buffer_(buffer) {}

  bool Fetch(uint64_t* index, std::shared_ptr<T>& m);  // NOLINT

  bool Latest(std::shared_ptr<T>& m);  // NOLINT

  bool FetchMulti(uint64_t fetch_size, std::vector<std::shared_ptr<T>>* vec);

  uint64_t channel_id() const { return channel_id_; }
  std::shared_ptr<BufferType> Buffer() const { return buffer_; }

  // --- PRD #96: Binary search over state history ring buffer ---------------

  /// Lower bound: returns the smallest index i in [Head, Tail] such that
  /// key_fn(*at(i)) >= target.  Returns Tail+1 if no such element exists.
  /// Requires the buffer to be sorted by key_fn (which tick-ordered buffers
  /// naturally are when keyed on timestamp or tick_id).
  /// Complexity: O(log N).
  template <typename KeyFn, typename Target>
  uint64_t LowerBound(KeyFn key_fn, const Target& target) const {
    std::lock_guard<std::mutex> lock(buffer_->Mutex());
    if (buffer_->Empty()) return 0;
    uint64_t lo = buffer_->Head();
    uint64_t hi = buffer_->Tail() + 1;
    while (lo < hi) {
      uint64_t mid = lo + (hi - lo) / 2;
      if (key_fn(*buffer_->at(mid)) < target) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    return lo;
  }

  /// Upper bound: returns the smallest index i such that
  /// key_fn(*at(i)) > target.
  template <typename KeyFn, typename Target>
  uint64_t UpperBound(KeyFn key_fn, const Target& target) const {
    std::lock_guard<std::mutex> lock(buffer_->Mutex());
    if (buffer_->Empty()) return 0;
    uint64_t lo = buffer_->Head();
    uint64_t hi = buffer_->Tail() + 1;
    while (lo < hi) {
      uint64_t mid = lo + (hi - lo) / 2;
      if (key_fn(*buffer_->at(mid)) <= target) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    return lo;
  }

  /// Exact binary search: returns pointer to element where key_fn matches
  /// target, or nullptr if not found.
  template <typename KeyFn, typename Target>
  std::shared_ptr<T> BinarySearch(KeyFn key_fn, const Target& target) const {
    uint64_t idx = LowerBound(key_fn, target);
    std::lock_guard<std::mutex> lock(buffer_->Mutex());
    if (idx <= buffer_->Tail()) {
      auto& elem = buffer_->at(idx);
      if (key_fn(*elem) == target) return elem;
    }
    return nullptr;
  }

  /// Range query: returns all elements where key_fn(elem) is in [lo, hi].
  template <typename KeyFn, typename Target>
  std::vector<std::shared_ptr<T>> Range(KeyFn key_fn, const Target& lo_val,
                                         const Target& hi_val) const {
    uint64_t from = LowerBound(key_fn, lo_val);
    uint64_t to   = UpperBound(key_fn, hi_val);
    std::vector<std::shared_ptr<T>> result;
    std::lock_guard<std::mutex> lock(buffer_->Mutex());
    if (buffer_->Empty()) return result;
    to = std::min(to, buffer_->Tail() + 1);
    for (uint64_t i = from; i < to; ++i) {
      result.push_back(buffer_->at(i));
    }
    return result;
  }

 private:
  uint64_t channel_id_;
  std::shared_ptr<BufferType> buffer_;
};

template <typename T>
bool ChannelBuffer<T>::Fetch(uint64_t* index,
                             std::shared_ptr<T>& m) {  // NOLINT
  std::lock_guard<std::mutex> lock(buffer_->Mutex());
  if (buffer_->Empty()) {
    return false;
  }

  if (*index == 0) {
    *index = buffer_->Tail();
  } else if (*index == buffer_->Tail() + 1) {
    return false;
  } else if (*index < buffer_->Head()) {
    auto interval = buffer_->Tail() - *index;
    AWARN << "channel[" << GlobalData::GetChannelById(channel_id_) << "] "
          << "read buffer overflow, drop_message[" << interval << "] pre_index["
          << *index << "] current_index[" << buffer_->Tail() << "] ";
    *index = buffer_->Tail();
  }
  m = buffer_->at(*index);
  return true;
}

template <typename T>
bool ChannelBuffer<T>::Latest(std::shared_ptr<T>& m) {  // NOLINT
  std::lock_guard<std::mutex> lock(buffer_->Mutex());
  if (buffer_->Empty()) {
    return false;
  }

  m = buffer_->Back();
  return true;
}

template <typename T>
bool ChannelBuffer<T>::FetchMulti(uint64_t fetch_size,
                                  std::vector<std::shared_ptr<T>>* vec) {
  std::lock_guard<std::mutex> lock(buffer_->Mutex());
  if (buffer_->Empty()) {
    return false;
  }

  auto num = std::min(buffer_->Size(), fetch_size);
  vec->reserve(num);
  for (auto index = buffer_->Tail() - num + 1; index <= buffer_->Tail();
       ++index) {
    vec->emplace_back(buffer_->at(index));
  }
  return true;
}

}  // namespace data
}  // namespace cyber
}  // namespace world

#endif  // CYBER_DATA_CHANNEL_BUFFER_H_
