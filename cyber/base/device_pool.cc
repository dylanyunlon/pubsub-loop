/******************************************************************************
 * device_pool.cc — Implementation of the process-global device memory pool.
 *
 * PRD: #355 [DistributedCore] Fix the default device pool getter
 *****************************************************************************/
#include "device_pool.h"

#include <cstdlib>
#include <new>

namespace world {
namespace base {

// ---- DevicePool::Impl placeholder ----------------------------------------
// In a full GPU build this would wrap cudaMalloc / cudaFreeHost etc.
// For now we fall back to aligned_alloc on all locations.
struct DevicePool::Impl {};

DevicePool::~DevicePool() = default;

void* DevicePool::alloc(std::size_t size, MemoryLocation loc) {
  if (size == 0) return nullptr;
  // Align to 256 bytes to match kWorldGlobalMemoryAlignment.
  constexpr std::size_t kAlign = 256;
  std::size_t alloc_size = (size + kAlign - 1) & ~(kAlign - 1);
  void* ptr = nullptr;
#ifdef _WIN32
  ptr = _aligned_malloc(alloc_size, kAlign);
#else
  if (posix_memalign(&ptr, kAlign, alloc_size) != 0) ptr = nullptr;
#endif
  if (!ptr) return nullptr;

  switch (loc) {
    case MemoryLocation::kDevice:
      device_used_.fetch_add(alloc_size, std::memory_order_relaxed);
      break;
    case MemoryLocation::kHostPinned:
      host_pinned_used_.fetch_add(alloc_size, std::memory_order_relaxed);
      break;
    case MemoryLocation::kHost:
      host_used_.fetch_add(alloc_size, std::memory_order_relaxed);
      break;
  }
  return ptr;
}

void DevicePool::free(void* ptr, MemoryLocation /*loc*/) {
  if (!ptr) return;
#ifdef _WIN32
  _aligned_free(ptr);
#else
  ::free(ptr);
#endif
  // Note: we don't track per-pointer sizes here; a production implementation
  // would use a side-table.  The atomic counters are approximate.
}

std::size_t DevicePool::used_bytes(MemoryLocation loc) const noexcept {
  switch (loc) {
    case MemoryLocation::kDevice:     return device_used_.load(std::memory_order_relaxed);
    case MemoryLocation::kHostPinned: return host_pinned_used_.load(std::memory_order_relaxed);
    case MemoryLocation::kHost:       return host_used_.load(std::memory_order_relaxed);
  }
  return 0;
}

// ---- Global singleton ----------------------------------------------------
namespace {
std::shared_ptr<DevicePool> s_default_pool;
std::once_flag s_init_flag;
std::atomic<bool> s_ready{false};
}  // namespace

DevicePool& get_default_device_pool() {
  if (!s_ready.load(std::memory_order_acquire) || !s_default_pool) {
    throw std::logic_error(
        "world::base::get_default_device_pool() called before "
        "base::initialize_device_pool().  Ensure initialize() runs before "
        "scheduler or transport init.  This is typically a startup-order race.");
  }
  return *s_default_pool;
}

void initialize_device_pool() {
  std::call_once(s_init_flag, [] {
    s_default_pool = std::make_shared<DevicePool>();
    s_ready.store(true, std::memory_order_release);
  });
}

bool device_pool_ready() noexcept {
  return s_ready.load(std::memory_order_acquire);
}

}  // namespace base
}  // namespace world
