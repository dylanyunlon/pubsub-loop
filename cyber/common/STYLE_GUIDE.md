# world::common Style Guide

## Tuning Policy Parameter Naming Convention

Established in Tuning Policy Cleanup Phase 2 (PRD #409).

### Type Parameters

Use `PascalCaseT` suffix:

| Canonical Name | Meaning | Deprecated aliases (removed) |
|---------------|---------|------------------------------|
| `OffsetT`     | Problem size / offset type | `PSizeT`, `ProblemSizeT`, `problem_size_t`, `N` |
| `OutputT`     | Output element type | `ResultT`, `OutT` |
| `InputT`      | Input element type | `InT`, `ValueT` (when used as input) |
| `KeyT`        | Sort/scan key type | `key_type`, `K` |
| `ValueT`      | Associated value type in key-value pairs | `V`, `val_type` |

### Value Parameters (non-type template parameters)

Use `kCamelCase`:

| Canonical Name       | Meaning | Deprecated aliases (removed) |
|---------------------|---------|------------------------------|
| `kBlockThreads`     | Threads per block | `BLOCK_SIZE`, `BLOCK_THREADS`, `BlockThreads` |
| `kItemsPerThread`   | Items processed per thread | `ITEMS_PER_THREAD`, `N` (when used as count) |
| `kTileSize`         | Tile size (= kBlockThreads × kItemsPerThread) | `TILE_SIZE` |
| `kWarpSpecialization` | Enable warp specialization | `WARP_SPEC` |

### Static constexpr Members

Policy structs expose computed values as `SCREAMING_SNAKE` for backward compatibility
with kernel launch code, but the template parameters themselves must follow `kCamelCase`:

```cpp
// Correct:
template <typename OffsetT, int kBlockThreads, int kItemsPerThread>
struct ReducePolicy {
  using offset_type = OffsetT;
  static constexpr int BLOCK_THREADS = kBlockThreads;
  static constexpr int ITEMS_PER_THREAD = kItemsPerThread;
  static constexpr int TILE_SIZE = kBlockThreads * kItemsPerThread;
};
```

### Enforcement

- `clang-tidy` check `readability-identifier-naming` configured in `.clang-tidy`
- Pre-commit grep guard: `grep -rn 'PSizeT\|ProblemSizeT\|problem_size_t\|BLOCK_SIZE\b' common/`
  must return zero results.
