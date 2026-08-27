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

#ifndef CYBER_DISTRIBUTED_CORE_ALIGNMENT_CHECK_H_
#define CYBER_DISTRIBUTED_CORE_ALIGNMENT_CHECK_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "cyber/distributed_core/include/memory_resource.h"

namespace distributed_core {

/// Runtime assertion that a pointer meets the world global-state alignment
/// requirement.  Aborts with a diagnostic message on violation.
inline void assert_world_aligned(
    const void* ptr,
    std::size_t required = kWorldGlobalMemoryAlignment) {
  auto addr = reinterpret_cast<std::uintptr_t>(ptr);
  if ((addr % required) != 0) {
    std::fprintf(
        stderr,
        "[DistributedCore] Alignment violation: ptr=%p, required=%zu, "
        "actual_offset=%zu\n",
        ptr, required, static_cast<std::size_t>(addr % required));
    std::abort();
  }
}

/// Non-aborting check — returns true if aligned.
inline bool is_world_aligned(
    const void* ptr,
    std::size_t required = kWorldGlobalMemoryAlignment) noexcept {
  auto addr = reinterpret_cast<std::uintptr_t>(ptr);
  return (addr % required) == 0;
}

}  // namespace distributed_core

#endif  // CYBER_DISTRIBUTED_CORE_ALIGNMENT_CHECK_H_
