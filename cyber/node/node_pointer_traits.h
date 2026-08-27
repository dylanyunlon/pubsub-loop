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

#ifndef CYBER_NODE_NODE_POINTER_TRAITS_H_
#define CYBER_NODE_NODE_POINTER_TRAITS_H_

#include <cstddef>
#include <memory>
#include <type_traits>

namespace apollo {
namespace cyber {
namespace node {

/**
 * @brief Lightweight node-channel pointer wrapper supporting
 *        std::pointer_traits::rebind.
 *
 * NodePtr<T> wraps either a raw pointer or an owning shared_ptr<T>.
 * It satisfies the NullablePointer, LegacyRandomAccessIterator (partially),
 * and Allocator-aware container pointer requirements via a proper
 * pointer_traits specialization.
 *
 * Key feature: `template<typename U> using rebind = NodePtr<U>;` enables
 * allocator-aware STL containers (vector, deque, etc.) to use node-channel
 * pointers without reinterpret_cast workarounds.
 */
template <typename T>
class NodePtr {
 public:
  using element_type = T;
  using difference_type = std::ptrdiff_t;

  /// Rebind support — required by Allocator-aware containers.
  template <typename U>
  using rebind = NodePtr<U>;

  /// Default constructs a null pointer.
  NodePtr() noexcept = default;

  /// Construct from raw pointer (non-owning).
  explicit NodePtr(T* raw) noexcept : ptr_(raw) {}

  /// Construct from shared_ptr (co-owning — bumps refcount).
  NodePtr(std::shared_ptr<T> sp) noexcept  // NOLINT(runtime/explicit)
      : ptr_(sp.get()), sp_(std::move(sp)) {}

  /// Copy/move — default is correct (shared_ptr handles ownership).
  NodePtr(const NodePtr&) = default;
  NodePtr(NodePtr&&) noexcept = default;
  NodePtr& operator=(const NodePtr&) = default;
  NodePtr& operator=(NodePtr&&) noexcept = default;

  /// Nullptr assignment.
  NodePtr& operator=(std::nullptr_t) noexcept {
    ptr_ = nullptr;
    sp_.reset();
    return *this;
  }

  T& operator*() const noexcept { return *ptr_; }
  T* operator->() const noexcept { return ptr_; }
  T* get() const noexcept { return ptr_; }

  explicit operator bool() const noexcept { return ptr_ != nullptr; }

  /// Access the underlying shared_ptr (if any).
  const std::shared_ptr<T>& shared() const noexcept { return sp_; }

  /// pointer_traits::pointer_to support.
  static NodePtr pointer_to(element_type& r) noexcept {
    return NodePtr(&r);
  }

  /// Equality operators.
  friend bool operator==(const NodePtr& a, const NodePtr& b) noexcept {
    return a.ptr_ == b.ptr_;
  }
  friend bool operator!=(const NodePtr& a, const NodePtr& b) noexcept {
    return a.ptr_ != b.ptr_;
  }
  friend bool operator==(const NodePtr& a, std::nullptr_t) noexcept {
    return a.ptr_ == nullptr;
  }
  friend bool operator==(std::nullptr_t, const NodePtr& a) noexcept {
    return a.ptr_ == nullptr;
  }
  friend bool operator!=(const NodePtr& a, std::nullptr_t) noexcept {
    return a.ptr_ != nullptr;
  }
  friend bool operator!=(std::nullptr_t, const NodePtr& a) noexcept {
    return a.ptr_ != nullptr;
  }

 private:
  T* ptr_ = nullptr;
  std::shared_ptr<T> sp_;  ///< Optional shared ownership.
};

}  // namespace node
}  // namespace cyber
}  // namespace apollo

/// std::pointer_traits specialization for NodePtr<T>.
namespace std {

template <typename T>
struct pointer_traits<apollo::cyber::node::NodePtr<T>> {
  using pointer = apollo::cyber::node::NodePtr<T>;
  using element_type = T;
  using difference_type = std::ptrdiff_t;

  template <typename U>
  using rebind = apollo::cyber::node::NodePtr<U>;

  static pointer pointer_to(element_type& r) noexcept {
    return pointer::pointer_to(r);
  }
};

}  // namespace std

#endif  // CYBER_NODE_NODE_POINTER_TRAITS_H_
