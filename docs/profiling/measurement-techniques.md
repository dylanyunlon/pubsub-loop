# Measurement Techniques: CPU vs. GPU vs. Batch GPU

PRD #420 — Profiling guide for pub/sub-loop developers.

## Quick Selection Guide

| Measurement target | Recommended technique | Precision | Reason |
|---|---|---|---|
| Individual `on_tick()` end-to-end latency | CPU timing | ~µs | Covers serialization + scheduling full chain |
| Single CUDA kernel execution time | GPU Event pair | ~0.5 µs | Stream-scoped, no CPU sync bubble in measurement |
| PipelineParallel multi-stage breakdown | Batch GPU Event array | ~0.5 µs/stage | Same stream, decomposes each stage independently |
| DDS/RTPS cross-process transport latency | CPU timing (synced clocks) | ~µs | GPU Events cannot cross process boundaries |

## Decision Tree

```
What are you measuring?
├── pub/sub end-to-end latency (includes serialization)?
│     └──▶ CPU timing (steady_clock)
├── Single CUDA kernel execution time?
│     └──▶ GPU Event pair (cudaEventRecord × 2)
└── Multi-kernel batch stage breakdown?
      └──▶ Batch GPU Event array (N+1 events)
            ├── ≤ 16 kernels in batch → Event overhead negligible
            └── > 16 kernels in batch → Consider NSight Compute instead
```

---

## 1. CPU Timing

### When to use

- `Individual::on_tick()` end-to-end latency
- DDS/RTPS transport pub→sub round-trip
- Scheduler dispatch latency (see `DispatchHistogram`)
- Any measurement where GPU kernels are not involved, or you need wall-clock inclusive of all overheads

### API

```cpp
#include <chrono>

auto t0 = std::chrono::steady_clock::now();
individual.on_tick(state);
auto t1 = std::chrono::steady_clock::now();
auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
```

### pub/sub-loop integration points

- **on_tick latency**: wrap the `on_tick()` call in the tick loop
- **Transport latency**: record `steady_clock::now()` at publish, embed in `IndividualState::publish_ns`, diff at subscriber
- **DispatchHistogram**: uses `steady_clock` internally, records into thread-local histogram

### Pitfall: GPU async execution

CPU timing does NOT include GPU kernel execution time unless you synchronize:

```cpp
auto t0 = std::chrono::steady_clock::now();
my_kernel<<<blocks, threads, 0, stream>>>(...);
cudaStreamSynchronize(stream);  // MUST sync to include kernel time
auto t1 = std::chrono::steady_clock::now();
```

Without the sync, `t1 - t0` only measures kernel *launch* overhead (~5-20 µs), not execution.

---

## 2. GPU Timing (CUDA Event Pair)

### When to use

- Single CUDA kernel execution time
- `device_select::dispatch` with specific tuning parameters
- Any GPU-side operation where you need ~0.5 µs precision without CPU sync bubbles during measurement

### API

```cpp
cudaEvent_t start, stop;
cudaEventCreate(&start);
cudaEventCreate(&stop);

cudaEventRecord(start, stream);
device_select::dispatch<..., kH100Tuning>(..., stream);
cudaEventRecord(stop, stream);

cudaEventSynchronize(stop);
float ms = 0.f;
cudaEventElapsedTime(&ms, start, stop);  // milliseconds, ~0.5 µs precision

cudaEventDestroy(start);
cudaEventDestroy(stop);
```

### pub/sub-loop integration points

- **neuron_compute kernels**: wrap individual kernel dispatches
- **world::reduce**: measure reduction kernel separately from host-side orchestration
- **data::segmented_reduce GPU path**: when GPU-accelerated reduce is used

### Pitfalls

1. **`cudaEventElapsedTime` requires stop to be completed** — always call `cudaEventSynchronize(stop)` first
2. **Cross-stream Events are meaningless** — Events on different streams have no ordering guarantee; use NSight Systems for cross-stream profiling
3. **Event creation/destruction has overhead** — create events once, reuse across iterations in benchmarks

---

## 3. Batch GPU Timing (CUDA Event Array)

### When to use

- PipelineParallel multi-stage pipelines
- world-state reduce multi-phase (load → compute → write-back)
- Any consecutive kernel submission where you need per-stage breakdown

### API

```cpp
constexpr int kStages = 4;
cudaEvent_t ev[kStages + 1];
for (auto& e : ev) cudaEventCreate(&e);

cudaEventRecord(ev[0], stream);
for (int i = 0; i < kStages; ++i) {
    pipeline_stage[i].dispatch(stream);
    cudaEventRecord(ev[i + 1], stream);
}
cudaStreamSynchronize(stream);

for (int i = 0; i < kStages; ++i) {
    float ms = 0.f;
    cudaEventElapsedTime(&ms, ev[i], ev[i + 1]);
    printf("Stage %d: %.3f ms\n", i, ms);
}
for (auto& e : ev) cudaEventDestroy(e);
```

### pub/sub-loop integration points

- **PipelineParallel**: insert events between pipeline stages
- **world-state snapshot**: decompose load/compute/writeback phases
- **neuron_compute batch inference**: measure per-batch kernel times

### Pitfalls

1. **Event array must be read after stream sync** — all `cudaEventElapsedTime` calls require the stop event to be completed
2. **Event insertion overhead: ~1-2 µs per event** — for batches with > 16 kernels, the measurement overhead itself becomes significant; consider NSight Compute for fine-grained profiling
3. **Do not reuse a single Event pair for batch measurement** — you only get total batch time, not per-stage breakdown

---

## Common Mistakes and Anti-Patterns

### 1. CPU timing that includes GPU kernels without sync

**Wrong**: measures launch overhead only
```cpp
auto t0 = now();
kernel<<<...>>>(...);  // async!
auto t1 = now();       // kernel hasn't finished
```

**Fix**: sync before second timestamp, or use GPU Events.

### 2. Cross-stream Event pairs

**Wrong**: Events on different streams have no ordering
```cpp
cudaEventRecord(start, stream_a);
kernel<<<..., stream_b>>>(...);  // different stream!
cudaEventRecord(stop, stream_b);
cudaEventElapsedTime(&ms, start, stop);  // UNDEFINED
```

**Fix**: use same stream for both events, or use NSight Systems.

### 3. Reusing single Event pair for batch

**Wrong**: only total time, no breakdown
```cpp
cudaEventRecord(start, stream);
for (int i = 0; i < N; ++i) stage[i].dispatch(stream);
cudaEventRecord(stop, stream);
// Only get total time, not per-stage
```

**Fix**: use N+1 Events to bracket each stage.

---

## Clock Precision Reference

| Clock source | Resolution | Overhead per call | Platform |
|---|---|---|---|
| `std::chrono::steady_clock` | ~1 ns (Linux), ~100 ns (Windows) | ~20 ns | All |
| `clock_gettime(CLOCK_MONOTONIC)` | ~1 ns | ~15 ns | Linux only |
| CUDA Event (`cudaEventElapsedTime`) | ~0.5 µs | ~1-2 µs per event pair | NVIDIA GPU |
| NSight Compute kernel profiling | ~10 ns | Replays kernel (high overhead) | NVIDIA GPU |
