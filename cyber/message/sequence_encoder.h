/******************************************************************************
 * Copyright 2024 The World Authors. All Rights Reserved.
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

#ifndef CYBER_MESSAGE_SEQUENCE_ENCODER_H_
#define CYBER_MESSAGE_SEQUENCE_ENCODER_H_

#include <cstdint>
#include <optional>

#include "cyber/message/detail/safe_multiply.h"

namespace world {
namespace message {

/**
 * @brief Encodes (writer_id, seq, timestamp) into a single uint64 sequence
 *        number for pub/sub message ordering.
 *
 * Uses compiler-intrinsic safe_multiply (PRD #271) instead of __int128.
 * Returns std::nullopt on overflow instead of silently truncating.
 */
struct SequenceEncoder {
  static constexpr uint64_t SEQ_SCALE = 1'000'000'000ULL;  // nanosecond scale

  [[nodiscard]] static std::optional<uint64_t>
  encode(uint32_t writer_id, uint32_t seq, uint64_t timestamp_ns) noexcept {
    uint64_t scaled_id;
    if (!detail::safe_multiply(
            static_cast<uint64_t>(writer_id), SEQ_SCALE, &scaled_id))
      return std::nullopt;  // overflow: writer_id × SEQ_SCALE exceeds u64

    uint64_t encoded;
    if (!detail::safe_multiply(scaled_id, timestamp_ns, &encoded))
      return std::nullopt;  // overflow: scaled × timestamp exceeds u64

    return encoded + seq;
  }
};

/**
 * @brief Monotonically-increasing state counter with overflow-safe scaling.
 *
 * Each call to next() returns counter_ × COUNTER_SCALE, using intrinsic
 * safe_multiply.  Counter wraps to 0 on overflow (returns 0 as sentinel).
 */
class StateCounter {
 public:
  static constexpr uint64_t COUNTER_SCALE = 65537ULL;

  uint64_t next() noexcept {
    uint64_t result;
    if (!detail::safe_multiply(counter_++, COUNTER_SCALE, &result)) {
      counter_ = 0;  // wrap on overflow
      return 0;
    }
    return result;
  }

  uint64_t current() const noexcept { return counter_; }
  void reset() noexcept { counter_ = 0; }

 private:
  uint64_t counter_ = 0;
};

}  // namespace message
}  // namespace world

#endif  // CYBER_MESSAGE_SEQUENCE_ENCODER_H_
