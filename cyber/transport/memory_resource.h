/******************************************************************************
 * memory_resource.h — Type-erased transport memory resource with alignment fix
 *
 * PRD #277: When individual state memory is allocated through the transport
 * layer's polymorphic interface, alignment must never degrade below the
 * transport minimum (64 bytes for cache-line, 256 for CUDA/ZeroCopy).
 *
 * Fix: default alignment parameter changed from alignof(std::max_align_t)
 * (16 bytes) to MIN_STATE_ALIGNMENT (64 bytes), and all allocations are
 * upgraded to max(requested, MIN_STATE_ALIGNMENT).
 *****************************************************************************/

#ifndef CYBER_TRANSPORT_MEMORY_RESOURCE_H_
#define CYBER_TRANSPORT_MEMORY_RESOURCE_H_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "cyber/common/log.h"

namespace world {
namespace cyber {
namespace transport {

/// Transport-layer minimum alignment.
/// Cache-line aligned for zero-copy SHM transfer.
/// For CUDA/ZeroCopy paths, callers should pass 256 explicitly.
inline constexpr size_t MIN_STATE_ALIGNMENT = 64;

// ─── MemoryResource (polymorphic base) ──────────────────────────────────────

class MemoryResource {
 public:
  virtual ~MemoryResource() = default;

  /// Allocate bytes with at least MIN_STATE_ALIGNMENT alignment.
  /// Alignment is always upgraded to max(alignment, MIN_STATE_ALIGNMENT).
  void* allocate(size_t bytes,
                 size_t alignment = MIN_STATE_ALIGNMENT) {
    alignment = std::max(alignment, MIN_STATE_ALIGNMENT);
    assert((alignment & (alignment - 1)) == 0 &&
           "alignment must be a power of 2");
    return do_allocate(bytes, alignment);
  }

  void deallocate(void* ptr, size_t bytes,
                  size_t alignment = MIN_STATE_ALIGNMENT) {
    alignment = std::max(alignment, MIN_STATE_ALIGNMENT);
    do_deallocate(ptr, bytes, alignment);
  }

  bool is_equal(const MemoryResource& other) const noexcept {
    return do_is_equal(other);
  }

 protected:
  virtual void* do_allocate(size_t bytes, size_t alignment) = 0;
  virtual void  do_deallocate(void* ptr, size_t bytes, size_t alignment) = 0;
  virtual bool  do_is_equal(const MemoryResource& other) const noexcept = 0;
};

// ─── TypedPool<T> ───────────────────────────────────────────────────────────

/// Typed memory pool that enforces alignment >= max(alignof(T), MIN_STATE_ALIGNMENT).
template <typename T>
class TypedPool : public MemoryResource {
 public:
  static constexpr size_t kRequiredAlignment =
      std::max(alignof(T), MIN_STATE_ALIGNMENT);

  struct Slot {
    T* ptr;
    size_t size;
  };

  Slot allocate_typed() {
    void* raw = allocate(sizeof(T), kRequiredAlignment);
    return Slot{static_cast<T*>(raw), sizeof(T)};
  }

  void deallocate_typed(Slot slot) {
    deallocate(slot.ptr, slot.size, kRequiredAlignment);
  }

 protected:
  void* do_allocate(size_t bytes, size_t alignment) override {
    // Upgrade alignment to meet type and transport requirements
    size_t required = std::max(alignment, kRequiredAlignment);
    if (alignment < required) {
      AINFO << "TypedPool: upgrading alignment from " << alignment
            << " to " << required;
    }
#if defined(_WIN32)
    void* ptr = _aligned_malloc(bytes, required);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, required, bytes) != 0) {
      ptr = nullptr;
    }
#endif
    if (!ptr) throw std::bad_alloc();
    return ptr;
  }

  void do_deallocate(void* ptr, size_t /*bytes*/,
                     size_t /*alignment*/) override {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
  }

  bool do_is_equal(const MemoryResource& other) const noexcept override {
    return this == &other;
  }
};

// ─── AlignmentVerifier (debug utility) ──────────────────────────────────────

/// Assert that a pointer meets the transport alignment requirement.
/// Use in debug builds to catch alignment regression.
inline bool verify_transport_alignment(const void* ptr,
                                        size_t required = MIN_STATE_ALIGNMENT) {
  return (reinterpret_cast<uintptr_t>(ptr) % required) == 0;
}

}  // namespace transport
}  // namespace cyber
}  // namespace world

#endif  // CYBER_TRANSPORT_MEMORY_RESOURCE_H_
