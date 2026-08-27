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
 * @file policy.h
 * @brief ScanByKeyPolicy — pure compile-time strategy for segmented scan.
 *
 * Split from the old monolithic DispatchScanByKey per PRD #410.
 * This file owns ONLY tile/thread/load configuration. No kernel launch.
 *
 * Naming convention follows common/STYLE_GUIDE.md:
 *   - Template value params: kCamelCase
 *   - Static constexpr members: SCREAMING_SNAKE (for kernel compat)
 */

#include <cstdint>
#include <type_traits>

namespace world {
namespace distributed_core {

/**
 * Block-level load algorithm hint.
 * Mirrors the CUB enum for familiarity; concrete backend chooses implementation.
 */
enum class BlockLoadAlgorithm : int {
  DIRECT = 0,           ///< Direct blocked load
  VECTORIZE = 1,        ///< Vectorized load (128-bit)
  TRANSPOSE = 2,        ///< Warp-striped → blocked transpose
  WARP_TRANSPOSE = 3,   ///< Warp-striped native
};

/**
 * ScanByKeyPolicy — stateless strategy struct.
 *
 * @tparam kBlockThreads    Threads per block (must be power-of-2, > 0)
 * @tparam kItemsPerThread  Items per thread (1..32)
 * @tparam kLoadAlgorithm   Block load strategy hint
 */
template <
    int kBlockThreads,
    int kItemsPerThread,
    BlockLoadAlgorithm kLoadAlgorithm = BlockLoadAlgorithm::DIRECT>
struct ScanByKeyPolicy {
  // ── Exported constants (SCREAMING_SNAKE for kernel launch compat) ──
  static constexpr int BLOCK_THREADS     = kBlockThreads;
  static constexpr int ITEMS_PER_THREAD  = kItemsPerThread;
  static constexpr int TILE_SIZE         = kBlockThreads * kItemsPerThread;
  static constexpr BlockLoadAlgorithm LOAD_ALGORITHM = kLoadAlgorithm;

  // ── Static validation ──
  static_assert(kBlockThreads > 0 && (kBlockThreads & (kBlockThreads - 1)) == 0,
                "kBlockThreads must be a positive power of 2");
  static_assert(kItemsPerThread >= 1 && kItemsPerThread <= 32,
                "kItemsPerThread must be in [1, 32]");

  // ── Helper: carry buffer element count for N items ──
  // Uses size_t to fix the INT_MAX OOM bug in the old DispatchScanByKey.
  static constexpr std::size_t carry_buffer_count(std::size_t num_items) {
    return (num_items + TILE_SIZE - 1) / TILE_SIZE;
  }
};

/**
 * Default policy for scan-by-key operations.
 * Tuned for broad compatibility; architecture-specific overrides
 * should specialize DefaultScanByKeyPolicy<Arch>.
 */
template <typename Arch = void>
using DefaultScanByKeyPolicy = ScanByKeyPolicy<256, 8, BlockLoadAlgorithm::DIRECT>;

}  // namespace distributed_core
}  // namespace world
