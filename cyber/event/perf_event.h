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

#ifndef CYBER_EVENT_PERF_EVENT_H_
#define CYBER_EVENT_PERF_EVENT_H_

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

#include "cyber/common/global_data.h"
#include "cyber/common/macros.h"

namespace world {
namespace cyber {
namespace event {

enum class EventType {
  SCHED_EVENT = 0,
  TRANS_EVENT = 1,
  TRY_FETCH_EVENT = 3,
  STREAM_PROGRESS_EVENT = 4
};

enum class TransPerf {
  TRANSMIT_BEGIN = 0,
  SERIALIZE = 1,
  SEND = 2,
  MESSAGE_ARRIVE = 3,
  OBTAIN = 4,  // only for shm
  DESERIALIZE = 5,
  DISPATCH = 6,
  NOTIFY = 7,
  FETCH = 8,
  CALLBACK = 9,
  TRANS_END
};

enum class SchedPerf {
  SWAP_IN = 1,
  SWAP_OUT = 2,
  NOTIFY_IN = 3,
  NEXT_RT = 4,
  RT_CREATE = 5,
};

class EventBase {
 public:
  virtual std::string SerializeToString() = 0;

  void set_eid(int eid) { eid_ = eid; }
  void set_etype(int etype) { etype_ = etype; }
  void set_stamp(uint64_t stamp) { stamp_ = stamp; }

  virtual void set_cr_id(uint64_t cr_id) { UNUSED(cr_id); }
  virtual void set_cr_state(int cr_state) { UNUSED(cr_state); }
  virtual void set_proc_id(int proc_id) { UNUSED(proc_id); }
  virtual void set_fetch_res(int fetch_res) { UNUSED(fetch_res); }

  virtual void set_msg_seq(uint64_t msg_seq) { UNUSED(msg_seq); }
  virtual void set_channel_id(uint64_t channel_id) { UNUSED(channel_id); }
  virtual void set_adder(const std::string& adder) { UNUSED(adder); }

 protected:
  int etype_;
  int eid_;
  uint64_t stamp_;
};

// event_id
// 1 swap_in
// 2 swap_out
// 3 notify_in
// 4 next_routine
class SchedEvent : public EventBase {
 public:
  SchedEvent() { etype_ = static_cast<int>(EventType::SCHED_EVENT); }

  std::string SerializeToString() override {
    std::stringstream ss;
    ss << etype_ << "\t";
    ss << eid_ << "\t";
    ss << common::GlobalData::GetTaskNameById(cr_id_) << "\t";
    ss << proc_id_ << "\t";
    ss << cr_state_ << "\t";
    ss << stamp_;
    return ss.str();
  }

  void set_cr_id(uint64_t cr_id) override { cr_id_ = cr_id; }
  void set_cr_state(int cr_state) override { cr_state_ = cr_state; }
  void set_proc_id(int proc_id) override { proc_id_ = proc_id; }

 private:
  int cr_state_ = 1;
  int proc_id_ = 0;
  uint64_t cr_id_ = 0;
};

// event_id = 1 transport
// 1 transport time
// 2 write_data_cache & notify listener
class TransportEvent : public EventBase {
 public:
  TransportEvent() { etype_ = static_cast<int>(EventType::TRANS_EVENT); }

  std::string SerializeToString() override {
    std::stringstream ss;
    ss << etype_ << "\t";
    ss << eid_ << "\t";
    ss << common::GlobalData::GetChannelById(channel_id_) << "\t";
    ss << msg_seq_ << "\t";
    ss << stamp_ << "\t";
    ss << adder_;
    return ss.str();
  }

  void set_msg_seq(uint64_t msg_seq) override { msg_seq_ = msg_seq; }
  void set_channel_id(uint64_t channel_id) override {
    channel_id_ = channel_id;
  }
  void set_adder(const std::string& adder) override { adder_ = adder; }

  static std::string ShowTransPerf(TransPerf type) {
    if (type == TransPerf::TRANSMIT_BEGIN) {
      return "TRANSMIT_BEGIN";
    } else if (type == TransPerf::SERIALIZE) {
      return "SERIALIZE";
    } else if (type == TransPerf::SEND) {
      return "SEND";
    } else if (type == TransPerf::MESSAGE_ARRIVE) {
      return "MESSAGE_ARRIVE";
    } else if (type == TransPerf::OBTAIN) {
      return "OBTAIN";
    } else if (type == TransPerf::DESERIALIZE) {
      return "DESERIALIZE";
    } else if (type == TransPerf::DISPATCH) {
      return "DISPATCH";
    } else if (type == TransPerf::NOTIFY) {
      return "NOTIFY";
    } else if (type == TransPerf::FETCH) {
      return "FETCH";
    } else if (type == TransPerf::CALLBACK) {
      return "CALLBACK";
    }
    return "";
  }

 private:
  std::string adder_ = "";
  uint64_t msg_seq_ = 0;
  uint64_t channel_id_ = std::numeric_limits<uint64_t>::max();
};

/// Stream progress tracking phases for individual-state streaming operations.
enum class StreamPerf {
  STREAM_BEGIN = 0,       ///< Individual batch stream initiated
  CHUNK_DISPATCHED = 1,   ///< A chunk of individual states dispatched
  CHUNK_CONFIRMED = 2,    ///< World confirmed receipt of chunk
  BACKPRESSURE_HIT = 3,   ///< Backpressure triggered (overflow)
  STREAM_COMPLETE = 4,    ///< All individuals in batch confirmed
};

/// Event for tracking progress of individual-state streaming operations.
/// Records how many individuals out of a total have been processed per tick.
class StreamProgressEvent : public EventBase {
 public:
  StreamProgressEvent() {
    etype_ = static_cast<int>(EventType::STREAM_PROGRESS_EVENT);
  }

  std::string SerializeToString() override {
    std::stringstream ss;
    ss << etype_ << "\t";
    ss << eid_ << "\t";
    ss << stream_id_ << "\t";
    ss << completed_ << "/" << total_ << "\t";
    ss << tick_seq_ << "\t";
    ss << stamp_;
    return ss.str();
  }

  void set_stream_id(uint64_t id) { stream_id_ = id; }
  void set_completed(uint64_t n) { completed_ = n; }
  void set_total(uint64_t n) { total_ = n; }
  void set_tick_seq(uint64_t seq) { tick_seq_ = seq; }

  uint64_t stream_id() const { return stream_id_; }
  uint64_t completed() const { return completed_; }
  uint64_t total() const { return total_; }
  uint64_t tick_seq() const { return tick_seq_; }

  static std::string ShowStreamPerf(StreamPerf type) {
    switch (type) {
      case StreamPerf::STREAM_BEGIN: return "STREAM_BEGIN";
      case StreamPerf::CHUNK_DISPATCHED: return "CHUNK_DISPATCHED";
      case StreamPerf::CHUNK_CONFIRMED: return "CHUNK_CONFIRMED";
      case StreamPerf::BACKPRESSURE_HIT: return "BACKPRESSURE_HIT";
      case StreamPerf::STREAM_COMPLETE: return "STREAM_COMPLETE";
      default: return "";
    }
  }

 private:
  uint64_t stream_id_ = 0;
  uint64_t completed_ = 0;
  uint64_t total_ = 0;
  uint64_t tick_seq_ = 0;
};

}  // namespace event
}  // namespace cyber
}  // namespace world

#endif  // CYBER_EVENT_PERF_EVENT_H_
