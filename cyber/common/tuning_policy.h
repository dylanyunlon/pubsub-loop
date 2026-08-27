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
 * @file tuning_policy.h
 * @brief Active policy selection interface for world algorithm tuning.
 *
 * All legacy TUNE_POLICY_* preprocessor macros have been removed.
 * The canonical interface is PolicySelector<Arch>:
 *   - block_threads:        threads per block (replaces TUNE_POLICY_BLOCK_SIZE_LEGACY)
 *   - items_per_thread:     items per thread  (replaces kDeprecatedItemsPerThread)
 *   - warp_specialization:  bool flag          (replaces TUNE_POLICY_SELECT_WARP_SPECIALIZATION)
 *
 * Removal record: see PRD #408 (Tuning policy cleanup 1/N)
 */

#include <cstdint>

namespace world {
namespace common {

/**
 * Architecture tag types for policy dispatch.
 * Each maps to a concrete set of tuning parameters.
 */
struct ArchGeneric {};
struct ArchSM70 {};
struct ArchSM80 {};
struct ArchSM90 {};

/**
 * PolicySelector — compile-time policy selection by architecture.
 *
 * Primary template provides conservative defaults suitable for any target.
 * Specializations may override for architecture-specific tuning.
 */
template <typename Arch>
struct PolicySelector {
  static constexpr int block_threads = 256;
  static constexpr int items_per_thread = 8;
  static constexpr bool warp_specialization = false;
  static constexpr int max_blocks_per_sm = 0;  // 0 = let runtime decide
  static constexpr int tile_size = block_threads * items_per_thread;
};

/** SM 7.0 (Volta): conservative warp scheduling */
template <>
struct PolicySelector<ArchSM70> {
  static constexpr int block_threads = 256;
  static constexpr int items_per_thread = 8;
  static constexpr bool warp_specialization = false;
  static constexpr int max_blocks_per_sm = 0;
  static constexpr int tile_size = block_threads * items_per_thread;
};

/** SM 8.0 (Ampere): warp specialization enabled */
template <>
struct PolicySelector<ArchSM80> {
  static constexpr int block_threads = 256;
  static constexpr int items_per_thread = 12;
  static constexpr bool warp_specialization = true;
  static constexpr int max_blocks_per_sm = 0;
  static constexpr int tile_size = block_threads * items_per_thread;
};

/** SM 9.0 (Hopper): larger tiles, warp specialization */
template <>
struct PolicySelector<ArchSM90> {
  static constexpr int block_threads = 512;
  static constexpr int items_per_thread = 16;
  static constexpr bool warp_specialization = true;
  static constexpr int max_blocks_per_sm = 0;
  static constexpr int tile_size = block_threads * items_per_thread;
};

/**
 * Helper to query policy for a given arch at compile time.
 */
template <typename Arch>
inline constexpr int policy_block_threads_v = PolicySelector<Arch>::block_threads;

template <typename Arch>
inline constexpr int policy_items_per_thread_v = PolicySelector<Arch>::items_per_thread;

template <typename Arch>
inline constexpr int policy_tile_size_v = PolicySelector<Arch>::tile_size;

}  // namespace common
}  // namespace world
