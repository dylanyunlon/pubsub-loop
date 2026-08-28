/******************************************************************************
 * device_pool.h — Process-global device memory pool for pub/sub transport.
 *
 * Allocates GPU VRAM and host-pinned memory for ShmSegment / ZeroCopyRegion.
 * The singleton is initialised by base::initialize() and guarded by a null
 * check so that early callers (scheduler init, transport setup) get a clear
 * diagnostic instead of a bare SIGSEGV.
 *
 * PRD: #355 [DistributedCore] Fix the default device pool getter
 *****************************************************************************/
#ifndef CYBER_BASE_DEVICE_POOL_H_
#define CYBER_BASE_DEVICE_POOL_H_

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace world {
namespace base {

/// Where to allocate memory.
enum class MemoryLocation : int {
  kDevice    = 0,  // GPU VRAM
  kHostPinned = 1, // Host-pinned (page-locked, DMA-accessible)
  kHost      = 2,  // Regular host heap
};

/// DevicePool — manages device and host-pinned memory regions.
///
/// This is not meant to be instantiated directly by user code.  Use
/// get_default_device_pool() to access the process-global instance.
class DevicePool {
 public:
  DevicePool() = default;
  ~DevicePool();

  DevicePool(const DevicePool&) = delete;
  DevicePool& operator=(const DevicePool&) = delete;

  /// Allocate @p size bytes at the given location.
  /// Returns nullptr on failure (never throws).
  void* alloc(std::size_t size, MemoryLocation loc);

  /// Free a region previously obtained from alloc().
  void free(void* ptr, MemoryLocation loc);

  /// Bytes currently outstanding at @p loc.
  std::size_t used_bytes(MemoryLocation loc) const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  mutable std::mutex mu_;
  std::atomic<std::size_t> device_used_{0};
  std::atomic<std::size_t> host_pinned_used_{0};
  std::atomic<std::size_t> host_used_{0};
};

/// Returns a reference to the process-global DevicePool.
///
/// @throws std::logic_error if base::initialize() has not been called yet.
///         The message includes a diagnostic to aid debugging startup races.
DevicePool& get_default_device_pool();

/// Initialise the global DevicePool.  Must be called once before any
/// transport or scheduler code touches get_default_device_pool().
/// Thread-safe: concurrent calls are serialised; the second call is a no-op.
void initialize_device_pool();

/// Returns true once initialize_device_pool() has completed.
bool device_pool_ready() noexcept;

}  // namespace base
}  // namespace world

#endif  // CYBER_BASE_DEVICE_POOL_H_
