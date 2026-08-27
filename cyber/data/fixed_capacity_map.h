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

#pragma once

/**
 * @file fixed_capacity_map.h
 * @brief O(1) ID→State hash map with explicit stream and memory resource.
 *
 * PRD #412: Make stream and memory resource explicit in fixed_capacity_map APIs.
 *
 * Key design decisions:
 *   - Constructor takes stream + memory resource (default = stream 0 + global alloc)
 *   - All operations execute on the bound stream (no implicit stream 0)
 *   - rebind_stream() for CRoutine stream switching
 *   - Uses TypeErasedMemoryResource from distributed_core when provided
 *
 * This is a CPU-side implementation. The GPU kernel dispatch version would
 * use the same interface but with CUDA stream and device memory semantics.
 */

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <vector>

namespace world {
namespace data {

/**
 * Opaque stream handle — maps to cudaStream_t / neuronStream_t / nullptr.
 * Using void* for portability; real builds typedef to the platform type.
 */
using StreamHandle = void*;

/**
 * Abstract memory resource interface.
 * Compatible with distributed_core::TypeErasedMemoryResource.
 */
class MemoryResourceBase {
 public:
  virtual ~MemoryResourceBase() = default;
  virtual void* allocate(std::size_t bytes, std::size_t alignment = 256) = 0;
  virtual void deallocate(void* ptr, std::size_t bytes,
                          std::size_t alignment = 256) = 0;
};

/**
 * Default memory resource using operator new/delete.
 */
class DefaultMemoryResource final : public MemoryResourceBase {
 public:
  static DefaultMemoryResource* instance() {
    static DefaultMemoryResource inst;
    return &inst;
  }

  void* allocate(std::size_t bytes, std::size_t alignment) override {
    (void)alignment;
    return ::operator new(bytes);
  }

  void deallocate(void* ptr, std::size_t bytes, std::size_t alignment) override {
    (void)bytes;
    (void)alignment;
    ::operator delete(ptr);
  }
};

/**
 * fixed_capacity_map<K, V> — open-addressing hash map with explicit resource.
 *
 * Fixed capacity, no rehash. Designed for ChannelBuffer<IndividualState>
 * where capacity is known at construction (= max individuals per channel).
 *
 * @tparam K  Key type (must be hashable and equality-comparable)
 * @tparam V  Value type (must be default-constructible and copyable)
 */
template <typename K, typename V>
class fixed_capacity_map {
 public:
  /**
   * Construct with explicit capacity, stream, and memory resource.
   *
   * @param capacity   Max number of entries (rounded up to power-of-2 internally)
   * @param stream     Execution stream for async operations (default = nullptr / stream 0)
   * @param resource   Memory allocator (default = global new/delete)
   */
  explicit fixed_capacity_map(
      std::size_t capacity,
      StreamHandle stream = nullptr,
      MemoryResourceBase* resource = nullptr)
      : stream_(stream),
        resource_(resource ? resource : DefaultMemoryResource::instance()),
        capacity_(next_power_of_2(capacity < 16 ? 16 : capacity)),
        mask_(capacity_ - 1),
        size_(0) {
    // Allocate slot arrays via memory resource
    std::size_t key_bytes = capacity_ * sizeof(K);
    std::size_t val_bytes = capacity_ * sizeof(V);
    std::size_t state_bytes = capacity_ * sizeof(SlotState);

    keys_ = static_cast<K*>(resource_->allocate(key_bytes, alignof(K)));
    values_ = static_cast<V*>(resource_->allocate(val_bytes, alignof(V)));
    states_ = static_cast<SlotState*>(
        resource_->allocate(state_bytes, alignof(SlotState)));

    std::memset(states_, 0, state_bytes);  // All slots EMPTY
  }

  ~fixed_capacity_map() {
    // Destroy live elements
    for (std::size_t i = 0; i < capacity_; ++i) {
      if (states_[i] == SlotState::OCCUPIED) {
        keys_[i].~K();
        values_[i].~V();
      }
    }
    resource_->deallocate(keys_, capacity_ * sizeof(K), alignof(K));
    resource_->deallocate(values_, capacity_ * sizeof(V), alignof(V));
    resource_->deallocate(states_, capacity_ * sizeof(SlotState),
                          alignof(SlotState));
  }

  // Non-copyable, moveable
  fixed_capacity_map(const fixed_capacity_map&) = delete;
  fixed_capacity_map& operator=(const fixed_capacity_map&) = delete;
  fixed_capacity_map(fixed_capacity_map&& other) noexcept
      : stream_(other.stream_),
        resource_(other.resource_),
        capacity_(other.capacity_),
        mask_(other.mask_),
        size_(other.size_),
        keys_(other.keys_),
        values_(other.values_),
        states_(other.states_) {
    other.keys_ = nullptr;
    other.values_ = nullptr;
    other.states_ = nullptr;
    other.size_ = 0;
  }

  /**
   * Insert key-value pair. Overwrites if key already exists.
   * Executes on bound stream (for async GPU version).
   */
  void insert(K key, V value) {
    std::lock_guard<std::mutex> lock(mutex_);
    assert(size_ < capacity_ && "fixed_capacity_map is full");

    std::size_t idx = hash(key) & mask_;
    while (true) {
      if (states_[idx] == SlotState::EMPTY ||
          states_[idx] == SlotState::TOMBSTONE) {
        new (&keys_[idx]) K(std::move(key));
        new (&values_[idx]) V(std::move(value));
        states_[idx] = SlotState::OCCUPIED;
        ++size_;
        return;
      }
      if (states_[idx] == SlotState::OCCUPIED && keys_[idx] == key) {
        values_[idx] = std::move(value);  // Overwrite
        return;
      }
      idx = (idx + 1) & mask_;  // Linear probing
    }
  }

  /**
   * Find value by key.
   * @return true if found, false otherwise
   */
  bool find(K key, V& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t idx = hash(key) & mask_;
    std::size_t start = idx;
    do {
      if (states_[idx] == SlotState::EMPTY) return false;
      if (states_[idx] == SlotState::OCCUPIED && keys_[idx] == key) {
        out = values_[idx];
        return true;
      }
      idx = (idx + 1) & mask_;
    } while (idx != start);
    return false;
  }

  /**
   * Erase by key.
   */
  void erase(K key) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t idx = hash(key) & mask_;
    std::size_t start = idx;
    do {
      if (states_[idx] == SlotState::EMPTY) return;
      if (states_[idx] == SlotState::OCCUPIED && keys_[idx] == key) {
        keys_[idx].~K();
        values_[idx].~V();
        states_[idx] = SlotState::TOMBSTONE;
        --size_;
        return;
      }
      idx = (idx + 1) & mask_;
    } while (idx != start);
  }

  std::size_t size() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
  }

  std::size_t capacity() const noexcept { return capacity_; }

  /**
   * Rebind to a different execution stream.
   * Called when CRoutine switches stream context.
   */
  void rebind_stream(StreamHandle new_stream) noexcept {
    stream_ = new_stream;
  }

  StreamHandle stream() const noexcept { return stream_; }
  MemoryResourceBase* resource() const noexcept { return resource_; }

 private:
  enum class SlotState : uint8_t {
    EMPTY = 0,
    OCCUPIED = 1,
    TOMBSTONE = 2,
  };

  static std::size_t next_power_of_2(std::size_t v) {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4;
    v |= v >> 8; v |= v >> 16; v |= v >> 32;
    return v + 1;
  }

  static std::size_t hash(const K& key) {
    return std::hash<K>{}(key);
  }

  StreamHandle stream_;
  MemoryResourceBase* resource_;
  std::size_t capacity_;
  std::size_t mask_;
  std::size_t size_;
  mutable std::mutex mutex_;

  K* keys_;
  V* values_;
  SlotState* states_;
};

}  // namespace data
}  // namespace world
