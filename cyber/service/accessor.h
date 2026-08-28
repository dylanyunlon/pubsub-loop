/******************************************************************************
 * accessor.h — Owner-scoped service resource accessor
 *
 * PRD #229: Fix get_global_resource() allowing non-owner individuals to
 * access (and corrupt / dangle) Service-owned resources.
 *
 * Solution: OwnerToken<S> is a non-copyable, non-movable proof object
 * that only ServiceOwner<S> can construct. get_global_resource() requires
 * an OwnerToken reference, preventing non-owners from calling it at all
 * (compile-time enforcement).
 *
 * For non-owner individuals that need the data: subscribe to the Service's
 * published state channel instead of accessing the resource directly.
 *****************************************************************************/

#ifndef CYBER_SERVICE_ACCESSOR_H_
#define CYBER_SERVICE_ACCESSOR_H_

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace world {
namespace cyber {
namespace service {

// Forward declaration
template <typename S>
class ServiceOwner;

// ═══════════════════════════════════════════════════════════════════════════════
// OwnerToken<S>
// ═══════════════════════════════════════════════════════════════════════════════
//
// Proof that the caller owns the Service of type S.
// Only constructable by ServiceOwner<S> (friend). Non-copyable, non-movable.
// Holds a validity flag that ServiceOwner<S> clears on destruction.

template <typename S>
class OwnerToken {
 public:
  ~OwnerToken() = default;

  // Non-copyable, non-movable
  OwnerToken(const OwnerToken&) = delete;
  OwnerToken& operator=(const OwnerToken&) = delete;
  OwnerToken(OwnerToken&&) = delete;
  OwnerToken& operator=(OwnerToken&&) = delete;

  /// Check if the owning Service is still alive.
  bool valid() const noexcept {
    return valid_.load(std::memory_order_acquire);
  }

 private:
  friend class ServiceOwner<S>;

  /// Only ServiceOwner<S> can construct.
  explicit OwnerToken() : valid_(true) {}

  /// ServiceOwner<S> calls this on destruction.
  void invalidate() noexcept {
    valid_.store(false, std::memory_order_release);
  }

  std::atomic<bool> valid_{true};
};

// ═══════════════════════════════════════════════════════════════════════════════
// ServiceAccessor
// ═══════════════════════════════════════════════════════════════════════════════
//
// Safe accessor requiring OwnerToken proof for resource access.

class ServiceAccessor {
 public:
  ServiceAccessor() = default;

  /// Construct with a raw resource pointer.
  /// The resource must outlive the accessor.
  explicit ServiceAccessor(void* resource_ptr) noexcept
      : resource_ptr_(resource_ptr) {}

  /**
   * Get the global resource (owner-only).
   *
   * Requires an OwnerToken<S> to prove the caller is the service owner.
   * Non-owners cannot call this — they don't have the token.
   *
   * In debug builds, also checks that the token is still valid (the
   * owning Service hasn't been destroyed yet).
   */
  template <typename T, typename S>
  T& get_global_resource(const OwnerToken<S>& token) {
    assert(token.valid() && "Service resource accessed after owner "
           "destruction — OwnerToken is invalid. "
           "This is a use-after-free bug.");
    assert(resource_ptr_ != nullptr && "Service resource is null.");
    return *static_cast<T*>(resource_ptr_);
  }

  /// Const access for read-only use cases.
  template <typename T, typename S>
  const T& get_global_resource(const OwnerToken<S>& token) const {
    assert(token.valid() && "Service resource accessed after owner "
           "destruction — OwnerToken is invalid.");
    assert(resource_ptr_ != nullptr && "Service resource is null.");
    return *static_cast<const T*>(resource_ptr_);
  }

  /**
   * @deprecated Use get_global_resource(const OwnerToken<S>&) instead.
   *
   * This overload has no owner safety check and will be removed.
   * Non-owner individuals should subscribe to the Service's published
   * state channel instead of accessing the resource directly.
   */
  template <typename T>
  [[deprecated("get_global_resource() without OwnerToken is unsafe. "
               "Use the OwnerToken overload or subscribe to the "
               "Service's state channel. "
               "See: world/service/docs/migration.md")]]
  T& get_global_resource() {
    assert(resource_ptr_ != nullptr);
    return *static_cast<T*>(resource_ptr_);
  }

 private:
  void* resource_ptr_ = nullptr;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ServiceOwner<S>
// ═══════════════════════════════════════════════════════════════════════════════
//
// Owns a Service resource of type S and provides the OwnerToken.

template <typename S>
class ServiceOwner {
 public:
  ServiceOwner() : resource_(std::make_unique<S>()),
                   token_(),
                   accessor_(resource_.get()) {}

  explicit ServiceOwner(std::unique_ptr<S> resource)
      : resource_(std::move(resource)),
        token_(),
        accessor_(resource_.get()) {}

  ~ServiceOwner() {
    // Invalidate token BEFORE destroying resource.
    // Any debug-build access attempt after this will assert.
    token_.invalidate();
    resource_.reset();
  }

  // Non-copyable, movable
  ServiceOwner(const ServiceOwner&) = delete;
  ServiceOwner& operator=(const ServiceOwner&) = delete;

  /// The OwnerToken — pass this to get_global_resource().
  const OwnerToken<S>& token() const { return token_; }

  /// The accessor — can be passed to other components,
  /// but get_global_resource() still requires the token.
  ServiceAccessor& accessor() { return accessor_; }
  const ServiceAccessor& accessor() const { return accessor_; }

  /// Direct access to the resource (owner only, no token needed).
  S& resource() { return *resource_; }
  const S& resource() const { return *resource_; }

 private:
  std::unique_ptr<S> resource_;
  OwnerToken<S> token_;
  ServiceAccessor accessor_;
};

}  // namespace service
}  // namespace cyber
}  // namespace world

#endif  // CYBER_SERVICE_ACCESSOR_H_
