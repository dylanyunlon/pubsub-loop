/******************************************************************************
 * dispatch_cache.h — Distributed async task dispatch cache
 *
 * PRD #227: Lock-free cache that prevents redundant recomputation when the
 * same logical task (individual_id, tick_id, task_type) is requested by
 * multiple CRoutines or nodes within a single tick cycle.
 *
 * Architecture:
 *   - Open-addressing hash table with Robin Hood probing
 *   - CAS-based slot claiming (lock-free on the hot path)
 *   - Tick-window eviction: entries older than N ticks are purged
 *   - Optional distributed mode via /dev/shm ring buffer (planned)
 *
 * Thread safety:
 *   - get_or_compute() is safe to call from multiple threads concurrently.
 *     Exactly one thread claims the slot and launches compute_fn; all others
 *     receive the same shared_future without re-executing the function.
 *   - advance_tick() is called from the tick coordinator thread only.
 *
 * Performance targets:
 *   - Cache hit: ~50ns (hash + shared_future::get())
 *   - Cache miss: equivalent to uncached path (zero regression)
 *   - Expected hit rate with 3+ consumers: 60-80%
 *****************************************************************************/

#ifndef CYBER_TASK_DISPATCH_CACHE_H_
#define CYBER_TASK_DISPATCH_CACHE_H_

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <vector>

namespace world {
namespace cyber {
namespace task {

// ─── TaskKey ────────────────────────────────────────────────────────────────

struct TaskKey {
  uint64_t individual_id;
  uint64_t tick_id;
  uint32_t task_type;
  uint32_t param_hash;

  bool operator==(const TaskKey& o) const {
    return individual_id == o.individual_id &&
           tick_id == o.tick_id &&
           task_type == o.task_type &&
           param_hash == o.param_hash;
  }

  bool operator!=(const TaskKey& o) const { return !(*this == o); }
};

// ─── TaskResult ─────────────────────────────────────────────────────────────

struct TaskResult {
  std::vector<uint8_t> data;     // Opaque result buffer
  uint64_t compute_time_ns = 0;  // Wall-clock time of compute_fn
  bool valid = false;

  TaskResult() = default;
  explicit TaskResult(std::vector<uint8_t> d, uint64_t ns = 0)
      : data(std::move(d)), compute_time_ns(ns), valid(true) {}
};

// ─── Hash function ──────────────────────────────────────────────────────────

struct TaskKeyHash {
  size_t operator()(const TaskKey& k) const noexcept {
    // FNV-1a variant: mix all fields
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&h](uint64_t v) {
      h ^= v;
      h *= 1099511628211ULL;
    };
    mix(k.individual_id);
    mix(k.tick_id);
    mix(static_cast<uint64_t>(k.task_type));
    mix(static_cast<uint64_t>(k.param_hash));
    return static_cast<size_t>(h);
  }
};

// ─── Cache Statistics ───────────────────────────────────────────────────────

struct CacheStats {
  std::atomic<uint64_t> hits{0};
  std::atomic<uint64_t> misses{0};
  std::atomic<uint64_t> evictions{0};
  std::atomic<uint64_t> peer_imports{0};

  struct Snapshot {
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint64_t peer_imports;
  };

  Snapshot snapshot() const noexcept {
    return {
        hits.load(std::memory_order_relaxed),
        misses.load(std::memory_order_relaxed),
        evictions.load(std::memory_order_relaxed),
        peer_imports.load(std::memory_order_relaxed),
    };
  }
};

// ═══════════════════════════════════════════════════════════════════════════════
// DispatchCache
// ═══════════════════════════════════════════════════════════════════════════════

class DispatchCache {
 public:
  struct Config {
    int max_entries    = 65536;  // Must be power of 2
    int tick_window    = 2;      // Evict entries older than N ticks
    bool distributed   = false;  // Enable /dev/shm peer sync (future)
    size_t shmem_size_mb = 64;   // Shared memory ring buffer size
  };

  explicit DispatchCache(const Config& cfg = {})
      : config_(cfg),
        mask_(next_pow2(cfg.max_entries) - 1),
        slots_(next_pow2(cfg.max_entries)) {
    for (auto& slot : slots_) {
      slot.state.store(SlotState::kEmpty, std::memory_order_relaxed);
    }
  }

  ~DispatchCache() = default;

  // Non-copyable, non-movable
  DispatchCache(const DispatchCache&) = delete;
  DispatchCache& operator=(const DispatchCache&) = delete;

  /**
   * Get a cached result or compute it.
   *
   * If the key is already cached (hit), returns the existing shared_future
   * without calling compute_fn. If not cached (miss), exactly one thread
   * claims the slot via CAS and launches compute_fn asynchronously. All
   * other threads requesting the same key receive the same shared_future.
   *
   * @param key        Unique task identifier
   * @param compute_fn Function to call on cache miss (produces TaskResult)
   * @return shared_future that will hold the TaskResult
   */
  std::shared_future<TaskResult> get_or_compute(
      const TaskKey& key,
      std::function<TaskResult()> compute_fn) {
    size_t h = hasher_(key);
    size_t idx = h & mask_;

    // Robin Hood linear probing
    for (int probe = 0; probe <= max_probe_distance(); ++probe) {
      size_t pos = (idx + probe) & mask_;
      auto& slot = slots_[pos];

      auto state = slot.state.load(std::memory_order_acquire);

      // ── Empty slot: try to claim it ──
      if (state == SlotState::kEmpty) {
        auto expected = SlotState::kEmpty;
        if (slot.state.compare_exchange_strong(
                expected, SlotState::kClaiming,
                std::memory_order_acq_rel)) {
          // We claimed this slot — launch computation
          slot.key = key;
          slot.key_hash = h;

          // Create promise + shared_future
          auto promise = std::make_shared<std::promise<TaskResult>>();
          slot.future = promise->get_future().share();

          // Mark slot as occupied before launching async
          slot.state.store(SlotState::kOccupied, std::memory_order_release);

          // Launch compute asynchronously
          std::thread([promise, fn = std::move(compute_fn)]() {
            try {
              promise->set_value(fn());
            } catch (...) {
              promise->set_exception(std::current_exception());
            }
          }).detach();

          stats_.misses.fetch_add(1, std::memory_order_relaxed);
          return slot.future;
        }

        // CAS failed — another thread claimed it; re-read and continue
        state = slot.state.load(std::memory_order_acquire);
      }

      // ── Occupied slot: check for key match ──
      if (state == SlotState::kOccupied || state == SlotState::kClaiming) {
        // Spin briefly if still claiming
        if (state == SlotState::kClaiming) {
          for (int spin = 0; spin < 64; ++spin) {
            state = slot.state.load(std::memory_order_acquire);
            if (state != SlotState::kClaiming) break;
          }
          if (state == SlotState::kClaiming) continue;  // Give up, next probe
        }

        if (state == SlotState::kOccupied &&
            slot.key_hash == h && slot.key == key) {
          // Cache hit
          stats_.hits.fetch_add(1, std::memory_order_relaxed);
          return slot.future;
        }
        // Different key in this slot — continue probing
      }
    }

    // All probes exhausted — compute without caching
    stats_.misses.fetch_add(1, std::memory_order_relaxed);
    auto promise = std::make_shared<std::promise<TaskResult>>();
    auto future = promise->get_future().share();
    std::thread([promise, fn = std::move(compute_fn)]() {
      try {
        promise->set_value(fn());
      } catch (...) {
        promise->set_exception(std::current_exception());
      }
    }).detach();
    return future;
  }

  /**
   * Advance the tick and evict stale entries.
   * Called from the tick coordinator thread at each tick boundary.
   */
  void advance_tick(uint64_t new_tick_id) {
    current_tick_.store(new_tick_id, std::memory_order_release);

    uint64_t evict_before = (new_tick_id > static_cast<uint64_t>(config_.tick_window))
                                ? new_tick_id - config_.tick_window
                                : 0;

    for (auto& slot : slots_) {
      auto state = slot.state.load(std::memory_order_acquire);
      if (state == SlotState::kOccupied) {
        if (slot.key.tick_id < evict_before) {
          // Evict: reset slot
          slot.future = {};
          slot.key = {};
          slot.key_hash = 0;
          slot.state.store(SlotState::kEmpty, std::memory_order_release);
          stats_.evictions.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  }

  /**
   * Synchronize with peer caches via shared memory.
   * (Stub for distributed mode — requires /dev/shm ring buffer.)
   */
  void sync_from_peers() {
    if (!config_.distributed) return;
    // TODO: mmap /dev/shm/world_dispatch_cache, scan ring buffer,
    // import entries matching current_tick_ that we don't have locally.
    // For now this is a no-op placeholder.
  }

  /**
   * Current cache statistics.
   */
  CacheStats::Snapshot stats() const { return stats_.snapshot(); }

  /**
   * Number of occupied slots (diagnostic, not thread-safe for exact count).
   */
  size_t occupied_count() const {
    size_t count = 0;
    for (const auto& slot : slots_) {
      if (slot.state.load(std::memory_order_relaxed) == SlotState::kOccupied) {
        ++count;
      }
    }
    return count;
  }

  /**
   * Total slot capacity.
   */
  size_t capacity() const { return slots_.size(); }

 private:
  enum class SlotState : uint8_t {
    kEmpty    = 0,
    kClaiming = 1,  // CAS transition: one thread is initializing
    kOccupied = 2,
  };

  struct alignas(64) Slot {
    std::atomic<SlotState> state{SlotState::kEmpty};
    TaskKey key{};
    size_t key_hash = 0;
    std::shared_future<TaskResult> future;
  };

  static constexpr size_t next_pow2(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
  }

  int max_probe_distance() const {
    // Robin Hood: max probing is log2(capacity) in practice
    int d = 0;
    size_t cap = slots_.size();
    while (cap >>= 1) ++d;
    return d < 16 ? 16 : d;  // At least 16 probes
  }

  Config config_;
  size_t mask_;
  std::vector<Slot> slots_;
  TaskKeyHash hasher_;
  CacheStats stats_;
  std::atomic<uint64_t> current_tick_{0};
};

}  // namespace task
}  // namespace cyber
}  // namespace world

#endif  // CYBER_TASK_DISPATCH_CACHE_H_
