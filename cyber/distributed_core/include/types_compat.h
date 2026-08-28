/******************************************************************************
 * types_compat.h — Deprecated aliases for transition period.
 *
 * Maps old scattered names (world_int32, WInt32, etc.) to the canonical
 * types in types.h.  Will be removed in the next major version.
 *****************************************************************************/
#ifndef CYBER_DISTRIBUTED_CORE_TYPES_COMPAT_H_
#define CYBER_DISTRIBUTED_CORE_TYPES_COMPAT_H_

#include "types.h"

namespace world {

[[deprecated("Use world::i32")]] typedef i32 world_int32;
[[deprecated("Use world::u32")]] typedef u32 WInt32;
[[deprecated("Use world::i64")]] typedef i64 world_int64;
[[deprecated("Use world::u64")]] typedef u64 WUInt64;

}  // namespace world

#endif  // CYBER_DISTRIBUTED_CORE_TYPES_COMPAT_H_
