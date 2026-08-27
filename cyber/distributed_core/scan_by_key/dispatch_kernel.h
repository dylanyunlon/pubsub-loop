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
 * @file dispatch_kernel.h
 * @brief DispatchScanByKeyKernel — kernel launch logic for segmented scan.
 *
 * Split from the old monolithic DispatchScanByKey per PRD #410.
 * This layer is responsible ONLY for:
 *   1. Allocating the carry buffer (using size_t, fixing the INT_MAX OOM bug)
 *   2. Computing grid dimensions from the policy
 *   3. Launching the kernel via the environment abstraction
 *
 * It does NOT choose the policy — that's the caller's responsibility.
 *
 * Usage:
 *   using Policy = ScanByKeyPolicy<256, 8>;
 *   DispatchScanByKeyKernel<Policy, KeysIt, ValsIt, OutIt, ScanOp, Env>
 *       ::Dispatch(env, keys, vals, output, num_items, scan_op, stream);
 */

#include <cstddef>
#include <cstdint>

#include "cyber/distributed_core/scan_by_key/policy.h"

namespace world {
namespace distributed_core {

/**
 * Default environment — wraps platform kernel launch.
 * Users can specialize for CUDA, Neuron, or host-side emulation.
 */
struct DefaultEnvironment {
  // Placeholder: in real build, this dispatches to cudaLaunchKernel / neuronLaunch
  template <typename KernelFunc, typename... Args>
  static int launch_kernel(KernelFunc func, int grid_blocks, int block_threads,
                           std::size_t shared_mem, void* stream, Args&&... args) {
    // Stub — actual kernel launch is platform-specific
    (void)func; (void)grid_blocks; (void)block_threads;
    (void)shared_mem; (void)stream;
    return 0;
  }
};

/**
 * DispatchScanByKeyKernel — the new kernel dispatch layer.
 *
 * @tparam PolicyT     A ScanByKeyPolicy instantiation
 * @tparam KeysInputIteratorT   Random-access iterator to keys
 * @tparam ValuesInputIteratorT Random-access iterator to values
 * @tparam ValuesOutputIteratorT Random-access output iterator
 * @tparam ScanOpT     Binary scan operator
 * @tparam EnvironmentT Launch environment (default: DefaultEnvironment)
 */
template <
    typename PolicyT,
    typename KeysInputIteratorT,
    typename ValuesInputIteratorT,
    typename ValuesOutputIteratorT,
    typename ScanOpT,
    typename EnvironmentT = DefaultEnvironment>
struct DispatchScanByKeyKernel {
  using Policy = PolicyT;

  /**
   * Main dispatch entry point.
   *
   * @param env          Launch environment
   * @param d_keys_in    Device pointer to input keys
   * @param d_values_in  Device pointer to input values
   * @param d_values_out Device pointer to output values
   * @param num_items    Total number of items (size_t — no INT_MAX limit)
   * @param scan_op      Binary scan operator
   * @param stream       Execution stream (platform-specific)
   * @return             0 on success, error code on failure
   */
  static int Dispatch(
      EnvironmentT env,
      KeysInputIteratorT d_keys_in,
      ValuesInputIteratorT d_values_in,
      ValuesOutputIteratorT d_values_out,
      std::size_t num_items,
      ScanOpT scan_op,
      void* stream = nullptr) {

    if (num_items == 0) return 0;

    // Carry buffer count — size_t arithmetic, no overflow
    const std::size_t num_tiles = Policy::carry_buffer_count(num_items);
    const int grid_blocks = static_cast<int>(
        (num_tiles < static_cast<std::size_t>(INT32_MAX))
            ? num_tiles : INT32_MAX);

    // Shared memory per block
    constexpr std::size_t shared_mem_bytes =
        Policy::TILE_SIZE * sizeof(typename std::iterator_traits<ValuesInputIteratorT>::value_type);

    // TODO: Allocate carry buffer (num_tiles elements) using env's allocator
    // TODO: Launch scan-by-key kernel
    // TODO: Launch carry-propagation kernel

    // Placeholder return — real implementation launches 2 kernels
    (void)d_keys_in; (void)d_values_in; (void)d_values_out;
    (void)scan_op; (void)grid_blocks; (void)shared_mem_bytes;
    return 0;
  }
};

}  // namespace distributed_core
}  // namespace world
