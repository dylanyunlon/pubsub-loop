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
 * @file policy_traits.h
 * @brief Compile-time introspection traits for tuning policies.
 *
 * Provides type-level queries against PolicySelector and algorithm-specific
 * policy structs, following the naming convention in common/STYLE_GUIDE.md:
 *   - Type parameters:   PascalCaseT  (OffsetT, KeyT, ValueT)
 *   - Value parameters:  kCamelCase   (kBlockThreads, kItemsPerThread)
 *
 * PRD #409: Tuning policy cleanup 2/N (parameter naming)
 * PRD #410: Separate from and deprecate DispatchScanByKey
 */

#include <type_traits>

namespace world {
namespace common {

// ── Primary trait: does a policy expose a given member? ──

namespace detail {

template <typename Policy, typename = void>
struct has_block_threads : std::false_type {};

template <typename Policy>
struct has_block_threads<Policy,
    std::void_t<decltype(Policy::BLOCK_THREADS)>> : std::true_type {};

template <typename Policy, typename = void>
struct has_items_per_thread : std::false_type {};

template <typename Policy>
struct has_items_per_thread<Policy,
    std::void_t<decltype(Policy::ITEMS_PER_THREAD)>> : std::true_type {};

template <typename Policy, typename = void>
struct has_tile_size : std::false_type {};

template <typename Policy>
struct has_tile_size<Policy,
    std::void_t<decltype(Policy::TILE_SIZE)>> : std::true_type {};

template <typename Policy, typename = void>
struct has_offset_type : std::false_type {};

template <typename Policy>
struct has_offset_type<Policy,
    std::void_t<typename Policy::offset_type>> : std::true_type {};

template <typename Policy, typename = void>
struct has_warp_specialization : std::false_type {};

template <typename Policy>
struct has_warp_specialization<Policy,
    std::void_t<decltype(Policy::warp_specialization)>> : std::true_type {};

}  // namespace detail

template <typename Policy>
inline constexpr bool has_block_threads_v = detail::has_block_threads<Policy>::value;

template <typename Policy>
inline constexpr bool has_items_per_thread_v = detail::has_items_per_thread<Policy>::value;

template <typename Policy>
inline constexpr bool has_tile_size_v = detail::has_tile_size<Policy>::value;

template <typename Policy>
inline constexpr bool has_offset_type_v = detail::has_offset_type<Policy>::value;

// ── Computed tile size (auto-derives if not explicit) ──

template <typename Policy, bool = has_tile_size_v<Policy>>
struct policy_tile_size_impl {
  static constexpr int value = Policy::TILE_SIZE;
};

template <typename Policy>
struct policy_tile_size_impl<Policy, false> {
  static_assert(has_block_threads_v<Policy> && has_items_per_thread_v<Policy>,
                "Policy must define either TILE_SIZE or both BLOCK_THREADS and ITEMS_PER_THREAD");
  static constexpr int value = Policy::BLOCK_THREADS * Policy::ITEMS_PER_THREAD;
};

template <typename Policy>
inline constexpr int policy_tile_size_v = policy_tile_size_impl<Policy>::value;

// ── Algorithm dispatch categories ──

/**
 * Tags for algorithm families. Used by dispatch layers to select
 * the correct policy and implementation.
 */
struct ReduceTag {};
struct ScanTag {};
struct SortTag {};
struct SelectTag {};

/**
 * Maps an algorithm tag to a default offset type.
 * Override by specializing for your tag.
 */
template <typename AlgorithmTag>
struct default_offset {
  using type = int;
};

template <typename AlgorithmTag>
using default_offset_t = typename default_offset<AlgorithmTag>::type;

/**
 * Reduce policy concept — requires BLOCK_THREADS and ITEMS_PER_THREAD.
 */
template <typename Policy>
inline constexpr bool is_valid_reduce_policy_v =
    has_block_threads_v<Policy> && has_items_per_thread_v<Policy>;

/**
 * Scan policy concept — requires BLOCK_THREADS and ITEMS_PER_THREAD.
 */
template <typename Policy>
inline constexpr bool is_valid_scan_policy_v =
    has_block_threads_v<Policy> && has_items_per_thread_v<Policy>;

/**
 * Sort policy concept — requires BLOCK_THREADS, ITEMS_PER_THREAD, and offset_type.
 */
template <typename Policy>
inline constexpr bool is_valid_sort_policy_v =
    has_block_threads_v<Policy> && has_items_per_thread_v<Policy> && has_offset_type_v<Policy>;

// ── ScanByKey deprecation shim (PRD #410) ──

/**
 * @deprecated Use ScanPolicy + key equality operator instead.
 *             ScanByKey is no longer a separate dispatch path.
 *             This shim exists for migration; remove after Phase 3.
 *
 * Maps old DispatchScanByKey<KeyT, ValueT, OffsetT> to the unified
 * ScanPolicy with key-equality as a runtime predicate.
 */
template <typename KeyT, typename ValueT, typename OffsetT = int>
struct [[deprecated("Use ScanPolicy with key equality predicate")]]
DispatchScanByKeyPolicy {
  using key_type = KeyT;
  using value_type = ValueT;
  using offset_type = OffsetT;
  static constexpr int BLOCK_THREADS = 256;
  static constexpr int ITEMS_PER_THREAD = 8;
  static constexpr int TILE_SIZE = BLOCK_THREADS * ITEMS_PER_THREAD;
};

// ── SegmentedSortPolicy helpers (PRD #411) ──

/**
 * Segment size categories for SegmentedSort dispatch.
 * PRD #411 corrected the ordering: small_segment < medium_segment.
 */
enum class SegmentCategory {
  kSmall,     ///< segment size ≤ small_threshold  (was wrongly "medium" before)
  kMedium,    ///< small_threshold < size ≤ medium_threshold
  kLarge      ///< size > medium_threshold
};

/**
 * Thresholds for segment categorization.
 * Swapped per PRD #411: small was 4096, medium was 256 — now corrected.
 */
struct SegmentThresholds {
  static constexpr int kSmallMax = 256;
  static constexpr int kMediumMax = 4096;
};

}  // namespace common
}  // namespace world
