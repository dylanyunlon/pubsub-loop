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

#ifndef CYBER_DISTRIBUTED_CORE_MEMORY_RESOURCE_H_
#define CYBER_DISTRIBUTED_CORE_MEMORY_RESOURCE_H_

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <algorithm>

namespace distributed_core {

/// World global-state memory alignment constant.
/// Matches cudaMalloc's 256-byte guarantee and maximises memory-transaction
/// coalescing on GPU world-state processors.
inline constexpr std::size_t kWorldGlobalMemoryAlignment = 256;

/**
 * @brief Type-erased polymorphic memory resource for world global-state storage.
 *
 * Default alignment is 256 bytes (kWorldGlobalMemoryAlignment) to match
 * cudaMalloc guarantees and enable optimal coalescing / vectorised loads.
 * Callers that explicitly pass a smaller alignment will still get at least
 * kWorldGlobalMemoryAlignment via the clamp in allocate()/deallocate().
 */
class TypeErasedMemoryResource {
 public:
  virtual ~TypeErasedMemoryResource() = default;

  void* allocate(std::size_t bytes,
                 std::size_t alignment = kWorldGlobalMemoryAlignment) {
    alignment = std::max(alignment, kWorldGlobalMemoryAlignment);
    assert((alignment & (alignment - 1)) == 0 && "alignment must be power of 2");
    return do_allocate(bytes, alignment);
  }

  void deallocate(void* ptr, std::size_t bytes,
                  std::size_t alignment = kWorldGlobalMemoryAlignment) {
    alignment = std::max(alignment, kWorldGlobalMemoryAlignment);
    do_deallocate(ptr, bytes, alignment);
  }

  /// Query the maximum alignment this resource supports.
  std::size_t max_alignment() const noexcept {
    return do_max_alignment();
  }

  bool is_equal(const TypeErasedMemoryResource& other) const noexcept {
    return do_is_equal(other);
  }

 protected:
  virtual void* do_allocate(std::size_t bytes, std::size_t alignment) = 0;
  virtual void do_deallocate(void* ptr, std::size_t bytes,
                             std::size_t alignment) = 0;
  virtual std::size_t do_max_alignment() const noexcept {
    return kWorldGlobalMemoryAlignment;
  }
  virtual bool do_is_equal(
      const TypeErasedMemoryResource& other) const noexcept = 0;
};

/**
 * @brief Wrapper that bypasses the 256-byte clamp for legacy code that
 *        genuinely needs a smaller alignment (e.g. 16-byte host allocations).
 *
 * This calls do_allocate on the inner resource directly with the requested
 * alignment, skipping the max() clamp.  Use only where the downstream
 * contract explicitly tolerates sub-256 alignment.
 */
class LegacyAlignmentWrapper : public TypeErasedMemoryResource {
 public:
  LegacyAlignmentWrapper(TypeErasedMemoryResource& inner,
                         std::size_t legacy_align)
      : inner_(inner), legacy_align_(legacy_align) {}

 protected:
  void* do_allocate(std::size_t bytes, std::size_t /*alignment*/) override {
    // Bypass clamp — use the legacy alignment directly on the inner resource.
    return inner_.allocate(bytes, legacy_align_);
  }

  void do_deallocate(void* ptr, std::size_t bytes,
                     std::size_t /*alignment*/) override {
    inner_.deallocate(ptr, bytes, legacy_align_);
  }

  std::size_t do_max_alignment() const noexcept override {
    return legacy_align_;
  }

  bool do_is_equal(
      const TypeErasedMemoryResource& other) const noexcept override {
    return this == &other;
  }

 private:
  TypeErasedMemoryResource& inner_;
  std::size_t legacy_align_;
};

/**
 * @brief Host-side aligned memory resource using posix_memalign / aligned_alloc.
 *
 * Useful for CPU-side world-state buffers that still need the 256-byte
 * alignment guarantee (e.g. staging buffers for DMA transfers).
 */
class AlignedHostMemoryResource : public TypeErasedMemoryResource {
 protected:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    void* ptr = nullptr;
#if defined(_WIN32)
    ptr = _aligned_malloc(bytes, alignment);
#else
    if (posix_memalign(&ptr, alignment, bytes) != 0) {
      ptr = nullptr;
    }
#endif
    return ptr;
  }

  void do_deallocate(void* ptr, std::size_t /*bytes*/,
                     std::size_t /*alignment*/) override {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
  }

  bool do_is_equal(
      const TypeErasedMemoryResource& other) const noexcept override {
    return this == &other;
  }
};

}  // namespace distributed_core

#endif  // CYBER_DISTRIBUTED_CORE_MEMORY_RESOURCE_H_
