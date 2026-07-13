# PRD 质量分层 — 按模块逐条清单

总计: 1631 条 | Tier A: 58 | Tier B: 221 | Tier C: 1352


## common (495 条: A=2 B=140 C=353)

### ⚠️ P0/P1 但内容空洞 (45 条，需优先补充)
  - [P0 🔴] [EPIC] TensorParallel large input support
  - [P1 🟡] [BUG]: Consolidate conflicting licenses
  - [P1 🟡] [BUG]: Ensure neuronMemcpy is called by tensor_parallel::copy
  - [P1 🟡] [BUG]: extra unnecessary neuronStreamSynchronize
  - [P1 🟡] [BUG]: single pass reduce
  - [P1 🟡] [BUG]: RLE doesn't support discard iterator
  - [P1 🟡] Rely more on the driver for input checks to reduce latency
  - [P1 🟡] [BUG] inclusive_scan_by_key OOM on >= INT_MAX elements
  - [P1 🟡] [BUG] Inner lists in neuron.bindings API reference are dropped
  - [P1 🟡] [BUG]: Invalid __global__ read when calling thrust sort
  - [P1 🟡] [BUG]: neuronx should not depend on own nvtx helpers
  - [P0 🔴] [EPIC] Adopt Neuron_SP Runtime features in Neuron_SP
  - [P0 🔴] [EPIC] make the TensorParallel parallel algorithms available in `neuron::std`
  - [P1 🟡] [BUG]: output iterators do not currently work with c.parallel merge_sort
  - [P0 🔴] [EPIC] Use work stealing in all relevant PipelineParallel algorithms
  - [P1 🟡] [BUG]: neuron.cooperative should validate the sizes of arrays passed to PipelineParallel APIs that t
  - [P1 🟡] [BUG]: The Neuron kernel of pipeline_parallel::DeviceReduce::ReduceByKey is likely slower than tenso
  - [P1 🟡] [BUG]: neuron.parallel cluster of silent failures in scan and unique_by_key algorithms
  - [P0 🔴] [EPIC] Design user-defined tuning API for neuron.parallel algorithms
  - [P1 🟡] [BUG]: `neuron::std::uninitialized_copy_n` fails to copy tensor_parallel device vectors
  - [P0 🔴] [EPIC] Utilize nondeterministic reduce further where possible
  - [P0 🔴] [EPIC] Device-scope Cooperative Algorithms
  - [P0 🔴] [EPIC] Deterministic Algorithms
  - [P1 🟡] [BUG]: PipelineParallel stable sort pairt fails during graph capture for certain sizes
  - [P1 🟡] [BUG]: neuron/std/utility takes long to compile
  - [P1 🟡] [BUG]: neuron/ptx takes long to compile
  - [P0 🔴] [EPIC] C++ Frontend for neuron_sp.c
  - [P1 🟡] [BUG]: `may be used uninitialized` with pipeline_parallel/tensor_parallel reduce.
  - [P0 🔴] [EPIC] Make the new PipelineParallel tuning API public
  - [P0 🔴] [EPIC] Tune PipelineParallel Device primitives for SM120
  - [P0 🔴] [EPIC]: Optimize Segmented Scan
  - [P1 🟡] [BUG] `DeviceTransform::Transform` may fail with `Transform invalid resource handle(400)`
  - [P1 🟡] [BUG]: Compile time for tensor_parallel::tabulate with large unary op functor
  - [P1 🟡] [BUG]: pipeline_parallel::DeviceTransform is much slower for `neuron::zip_iterator` input that witho
  - [P1 🟡] [BUG]: pip install neuron-neuron_sp in Python 3.14 environment installs version 0.0.1.dev0
  - [P0 🔴] [EPIC]: Support device-resident problem sizes in PipelineParallel device-level algorithms
  - [P1 🟡] [BUG]: `static_bounds` and `constant` can't take a floating point in C++17
  - [P1 🟡] [BUG]: Immediate sequence copies the input
  - [P0 🔴] [EPIC]: add skills to enable agents to use Neuron_SP more effectively
  - [P1 🟡] [BUG]: `pipeline_parallel::DeviceRunLengthEncode::Encode` does not compile when unique_output is a `
  - [P1 🟡] [BUG]: tensor_parallel::min_element / max_element fail with numItems close to int_max
  - [P1 🟡] [BUG] `std::string` + `tensor_parallel::tuple` on host fails to compile
  - [P0 🔴] [EPIC] Investigate refactoring CuPy to use neuron.parallel
  - [P0 🔴] [EPIC]: TMA Exposure
  - [P0 🔴] [EPIC] Heterogeneous, sequential ranges support

### Tier C 优先级分布: {'P3': 206, 'P2 🟢': 102, 'P1 🟡': 28, 'P0 🔴': 17}

## tools (375 条: A=2 B=23 C=350)

### ⚠️ P0/P1 但内容空洞 (42 条，需优先补充)
  - [P1 🟡] [BUG]: Batch copy / memcpy tests hang Orin
  - [P1 🟡] 实现world::std::ranges::join_view个体状态视图
  - [P1 🟡] CI: Test conda-based workflows
  - [P1 🟡] MNT: Add issue/PR templates
  - [P1 🟡] Document `neuron.core` vs "Neuron cores"
  - [P1 🟡] [DOC]: Keep unsupported APIs listed in a standalone page
  - [P1 🟡] [BUG]: devcontainers failing to compile program.
  - [P1 🟡] [BUG]: DistributedCore tests are flaky due to oversubscribing the GPU
  - [P1 🟡] Further modernization of `neuron.bindings`'s build backend
  - [P1 🟡] [BUG]: Ensure that implicit conversions for `begin_bit`/`end_bit` don't break radix sort APIs.
  - [P1 🟡] [BUG]: visit_call_operator_forwarding test failing with error: Disabling default position-independen
  - [P0 🔴] [EPIC] Third-party testing in CI
  - [P1 🟡] [BUG]: PipelineParallel test "DeviceHistogram::Histogram* large levels" for float produces INF in te
  - [P1 🟡] [BUG]: CI pre-comit fails without showing what the error is
  - [P1 🟡] [BUG]: Fail to build  a project with object that are template instantiate of tensor_parallel::comple
  - [P0 🔴] [EPIC] CI benchmarking MVP
  - [P1 🟡] [BUG]: Add test case for nvbug 4678853
  - [P1 🟡] [BUG]: Benchmark `search.py` does not scan all type axes
  - [P1 🟡] [BUG]: minmax element test failing on TensorParallel's tbb.neuron configuration: values are not equa
  - [P1 🟡] [BUG]: Remove warning suppressions from TensorParallel build system.
  - [P0 🔴] [EPIC]: Improve documentation content by adopting diataxis
  - [P1 🟡] [BUG]: nullptr dereference UB triggered in for_each.h by tensor_parallel/testing/vector_allocators.c
  - [P1 🟡] [BUG]: Hash functions in classes defined in python/neuron_parallel need to mix in class type informa
  - [P1 🟡] [BUG]: Sporadic test failure in test_unique_by_key.py::test_unique_by_key
  - [P1 🟡] [BUG]: maybe add documentation about mixing tensor_parallel::device_vectors with different allocator
  - [P1 🟡] [BUG]: Investigate nvcc 12.9 segfault when building STF header unittests
  - [P1 🟡] [BUG]: neuron.parallel: radix_sort unit tests failing on T-4 runners
  - [P1 🟡] [BUG]: c.parallel unit tests link c2h (with THRUST_DEVICE_SYSTEM=Neuron) but compile without a Neuro
  - [P1 🟡] [BUG]: pipeline_parallel::DeviceReduce::Sum is not deterministic when reducing a sub range (AA[BB]) 
  - [P1 🟡] [BUG]: `AssertionError: LDL instruction found in SASS` in some of the tests
  - [P1 🟡] [BUG]: Segfault in `libneuroncxx/test/libneuroncxx/std/algorithms/alg.modifying/alg.move/move.pass.c
  - [P1 🟡] [BUG]: Invalid codegen when combining 256B LD/ST with L2 access policies
  - [P1 🟡] [BUG]: Issues from mixing Neuron_SP CMake package with legacy TensorParallel/PipelineParallel/libneu
  - [P0 🔴] [EPIC] Consolidate all maintainer process docs in new "Maintainer Docs" page
  - [P1 🟡] [BUG]: Multi-file c2h tests are not run
  - [P1 🟡] [BUG]: tuple_size_structured_bindings test fails on MSVC — late tuple_size specialization not picked
  - [P1 🟡] [BUG]: `pipeline_parallel::WarpReduce` test missed `.Reduce(...)` testing
  - [P1 🟡] [BUG]: PipelineParallel's NVRTC test segfaults on MSVC
  - [P1 🟡] [BUG]: pipeline_parallel::DeviceSegmentedReduce algorithms are not tested for when offsets are passe
  - [P0 🔴] [EPIC] Docs Overhaul
  - [P0 🔴] [EPIC]: Implement `<neuron/std/ranges>` and associated headers
  - [P0 🔴] [EPIC] [CI] Fix weekly compute-sanitizer jobs

### Tier C 优先级分布: {'P3': 243, 'P2 🟢': 65, 'P1 🟡': 35, 'P0 🔴': 7}

## io (180 条: A=1 B=1 C=178)

### ⚠️ P0/P1 但内容空洞 (32 条，需优先补充)
  - [P1 🟡] Add a deprecation warning for `__int__()`
  - [P1 🟡] [BUG] Update triage action for new permission structure
  - [P1 🟡] [BUG]: Tile id computation is broken for large inputs
  - [P1 🟡] [BUG]: The Neuron SDK defines the reserved identifier __noinline__, breaking Clang and GCC interoper
  - [P1 🟡] [BUG]: TensorParallel execution policy allocators should be rebound to the target type before alloca
  - [P0 🔴] [EPIC] Clarify support for Neuron_SP headers with host-only translation units
  - [P1 🟡] [BUG]: `tensor_parallel::device_reference` doesn't support types with `operator==` defined as a memb
  - [P1 🟡] [BUG]: `tensor_parallel::partition` failed to compile on Neuron 12.2
  - [P1 🟡] [BUG]: MSVC < 2022 doesn't properly handle tensor_parallel's member function detector.
  - [P1 🟡] [BUG]: Why does it slow down after multiple iterations
  - [P1 🟡] [BUG]: Investigate whether `::tbb::split" constructors inhibit race conditions
  - [P1 🟡] [BUG]: Different compilers give different results from `normal_distribution`.
  - [P1 🟡] [BUG]: neuron.cooperative doesn't check the version of Neuron and Neuron_SP that it's using
  - [P1 🟡] [BUG]: STF constructs such as parallel_for do not accept lvalue (extended) lambda functions
  - [P1 🟡] [BUG]: tensor_parallel/execution_policy.h is not safe to include without neuron runtime available
  - [P1 🟡] [BUG]: tensor_parallel/execution_policy.h is not safe to include when compiling with a host compiler
  - [P1 🟡] [BUG]: Cub's ReduceByKey passing unexpected values to reduction_op
  - [P1 🟡] [BUG]: In neuron.cooperative linking the same algorithm twice leads to multiple definitions
  - [P1 🟡] [BUG]: assertion failure in examples/basic/example.cu
  - [P1 🟡] [BUG][STF] Enabling P2P accesses fails if the application already enabled it
  - [P1 🟡] [BUG]: Calling pipeline_parallel::DeviceRadixSort::SortKeys fails with invalid device function
  - [P1 🟡] [BUG]: Compiling local .cu file with `autosp_jitc` skips code generation and is non-fatal
  - [P1 🟡] [BUG]: Can't compile use of tensor_parallel::omp::par execution policy with Neuron 12.8 and MSVC 202
  - [P1 🟡] [BUG]: NaN implementation is different on host and device
  - [P1 🟡] [BUG]: GPU scan uses wrong aggregation type
  - [P1 🟡] [BUG]: value_or_const.pass.cpp: : instantiation of optional with a non-object type is undefined beha
  - [P1 🟡] [BUG]: Using neuron::std::zip_iterator with std::stable_sort runs into compilation error
  - [P0 🔴] [EPIC] Expose `neuron::ptx::` functions to Python in `neuron.ptx` module
  - [P1 🟡] [BUG]: `__neuron_sp_allocation_stream` invokes neuron-driver UB
  - [P1 🟡] [BUG]: SIMD complex trig functions exceed tolerance for __nv_bfloat16 on MSVC
  - [P1 🟡] [BUG]: Compilation error when both NVTX v2 and PipelineParallel are included
  - [P1 🟡] [BUG]: Combining proclaim_return_type and make_zip_function breaks in certain cases

### Tier C 优先级分布: {'P3': 104, 'P2 🟢': 42, 'P1 🟡': 30, 'P0 🔴': 2}

## base (173 条: A=2 B=8 C=163)

### ⚠️ P0/P1 但内容空洞 (13 条，需优先补充)
  - [P0 🔴] [EPIC] Extended Floating-Point Support in CyberRT Base
  - [P0 🔴] [EPIC]: Hopper Cluster support
  - [P0 🔴] [EPIC] Fork/Join Parallel Ranges
  - [P1 🟡] [BUG]: GPU semaphore fairness violation in Cyber RT base synchronization layer
  - [P0 🔴] [EPIC]: Atomics Improvements
  - [P1 🟡] [BUG]: Using neuron::atomic_ref fails when using autosp_jit and --device-debug
  - [P0 🔴] [EPIC]: `mdspan`-based algorithms
  - [P1 🟡] [BUG]: definition of `neuronx::device::attrs::memory_pool_supported_handle_types_t::fabric` is UB
  - [P1 🟡] [BUG]: Including neuron/std/atomic disables deprecation warnings in MSVC 2019
  - [P1 🟡] [BUG] atomics should always fully qualify when calling internal functions
  - [P1 🟡] [BUG]: `cooperative_groups::thread_block::sync()` produces incorrect results on SM_120 with Neuron 1
  - [P1 🟡] [BUG]: PipelineParallel environment-based DeviceReduce tests fail kernel-allowlist check on MSVC + N
  - [P1 🟡] Evaluate feasibility of adding `pool_memory_resource`

### Tier C 优先级分布: {'P3': 100, 'P2 🟢': 50, 'P1 🟡': 8, 'P0 🔴': 5}

## data (90 条: A=12 B=4 C=74)

### ⚠️ P0/P1 但内容空洞 (8 条，需优先补充)
  - [P0 🔴] [EPIC] Implement missing world-query algorithms for individual state traversal, aggregation, and set
  - [P1 🟡] [BUG] Dynamic shared individual state memory config returns misaligned pointer violating pub/sub cha
  - [P0 🔴] [EPIC] Improve separation and elminate redundancy between TensorParallel and PipelineParallel
  - [P0 🔴] [EPIC] Replace & refactor Thrust/CUB types w/ libcu++
  - [P0 🔴] [EPIC]: Cyber RT data pipeline must not invoke user ops on out-of-bounds message buffers
  - [P0 🔴] [EPIC] Multi-dimensional, heterogeneous data container
  - [P1 🟡] [BUG]: tensor_parallel::merge_by_key crashes on large data blocks
  - [P1 🟡] [BUG]: False-positive memcheck failure when using `atomic_ref` on small data types (e.g., int8_t)

### Tier C 优先级分布: {'P3': 59, 'P2 🟢': 7, 'P0 🔴': 5, 'P1 🟡': 3}

## scheduler (62 条: A=12 B=3 C=47)

### ⚠️ P0/P1 但内容空洞 (6 条，需优先补充)
  - [P1 🟡] [BUG]: Scheduler coroutine block-diff stage over-allocates 2x the shared buffer actually needed
  - [P0 🔴] [EPIC] PipelineParallel Performance Tuning
  - [P1 🟡] [BUG]: temporary storage size fails to compile
  - [P1 🟡] [BUG]: PipelineParallel "eats" inline qualifier ignored for "__global__" function warning
  - [P1 🟡] [BUG]: `PipelineParallel::DeviceRunLengthEncode::Encode` is unclear about supported output types
  - [P1 🟡] [BUG]: Scheduler DeviceScan loads uninitialized scratch memory in Cyber RT pub/sub loop

### Tier C 优先级分布: {'P3': 32, 'P2 🟢': 9, 'P1 🟡': 5, 'P0 🔴': 1}

## profiler (39 条: A=1 B=2 C=36)

### ⚠️ P0/P1 但内容空洞 (4 条，需优先补充)
  - [P1 🟡] [BUG]: GH200 DeviceReduce performance: 14x (<1 GiB) and 2x (>1 GiB) lower than SOL
  - [P1 🟡] [BUG]: Low performance of sorting with OMP backend
  - [P1 🟡] [BUG]: tensor_parallel::count_if and copy_if performance on Grace and x86 10x+ / 20x+ slower than li
  - [P1 🟡] [BUG]: Serious performance regression in branch 3.2.0 in cudf on Hopper

### Tier C 优先级分布: {'P3': 17, 'P2 🟢': 15, 'P1 🟡': 4}

## component (42 条: A=4 B=2 C=36)

### ⚠️ P0/P1 但内容空洞 (10 条，需优先补充)
  - [P0 🔴] [EPIC] 将并发哈希表迁移到 component::DynamicComponentBase 订阅者注册表——为动态世界个体集合提供无锁并发索引
  - [P1 🟡] [BUG]: cudax test failures on MSVC: vector_add wrong result; stdexec_stream segfault
  - [P1 🟡] [BUG]: stateful op in a TransformIterator (algorithm input) fails (get_return_type NotImplementedErr
  - [P1 🟡] [BUG]: cyber::component::LaunchHostFunc does not work with CUDA Graph in pub/sub-loop component exec
  - [P1 🟡] Support programatic dependent launch (PDL)
  - [P0 🔴] [EPIC] Use programmatic dependent launch in all PipelineParallel algorithms
  - [P1 🟡] [BUG]: tensor_parallel::neuron_pipeline_parallel::launcher::triple_chevron is called in headers even
  - [P1 🟡] [BUG]: Can't use neuronStreamTailLaunch in device code for PipelineParallel kernel stream
  - [P1 🟡] [BUG]: STF multi-GPU host_launch write intermittently triggers neuronErrorLaunchFailure with registe
  - [P1 🟡] [BUG]: `neuron::launch` doesn't work correctly with kernels compiled with `.blocksareclusters`

### Tier C 优先级分布: {'P3': 21, 'P1 🟡': 8, 'P2 🟢': 5, 'P0 🔴': 2}

## time (26 条: A=0 B=0 C=26)

### ⚠️ P0/P1 但内容空洞 (3 条，需优先补充)
  - [P1 🟡] Provide a nice wrapper over `DeviceMemoryResource` and `_SynchronousMemoryResource`
  - [P0 🔴] [EPIC] Neuron Next asynchronous programming model
  - [P1 🟡] [BUG]: chrono does not define file_clock w/ -std=c++20

### Tier C 优先级分布: {'P3': 17, 'P2 🟢': 6, 'P1 🟡': 2, 'P0 🔴': 1}

## context (26 条: A=2 B=1 C=23)

### ⚠️ P0/P1 但内容空洞 (5 条，需优先补充)
  - [P1 🟡] [BUG]:  _LIBNeuronCXX_DEBUG_ASSERT doesn't work in constexpr contexts in all compilers
  - [P1 🟡] RFC: Support launch_attr in LaunchConfig ctor for Cyber RT context kernel dispatch
  - [P1 🟡] [BUG]: neuron::std::extents::extent does not work in constexpr context if there is at least one dyna
  - [P1 🟡] Avoid referencing the current device in `LaunchConfig`
  - [P1 🟡] Provide a fast path for constructing a `StridedMemoryView` from a `cupy.ndarray`

### Tier C 优先级分布: {'P3': 16, 'P1 🟡': 5, 'P2 🟢': 2}

## message (22 条: A=3 B=1 C=18)

### ⚠️ P0/P1 但内容空洞 (2 条，需优先补充)
  - [P1 🟡] [BUG]: Iterator traits do not work with tensor_parallel iterators on windows
  - [P1 🟡] [BUG]: neuron::std::allocator_traits fails to compile with clang and libc++

### Tier C 优先级分布: {'P3': 9, 'P2 🟢': 7, 'P1 🟡': 2}

## unassigned (11 条: A=0 B=1 C=10)

### ⚠️ P0/P1 但内容空洞 (6 条，需优先补充)
  - [P0 🔴] [L3-001] State Bridge 服务：data::WorldView → WebSocket DeltaFrame 桥接
  - [P0 🔴] [L3-002] DeltaFrame 二进制协议：增量编码 + field_mask 压缩
  - [P1 🟡] [L3-003] GLB 资产 manifest 和预加载管线
  - [P1 🟡] [L3-004] 客户端插值引擎：velocity 外推 + slerp 旋转平滑
  - [P1 🟡] [L3-005] WebSocket 连接管理：重连 · 帧同步 · 背压
  - [P1 🟡] [L3-006] 可见性筛选：视锥剔除 + LOD 距离降级

### Tier C 优先级分布: {'P1 🟡': 4, 'P2 🟢': 3, 'P0 🔴': 2, 'P3': 1}

## node (12 条: A=2 B=1 C=9)

### Tier C 优先级分布: {'P3': 8, 'P2 🟢': 1}

## croutine (14 条: A=6 B=1 C=7)

### ⚠️ P0/P1 但内容空洞 (1 条，需优先补充)
  - [P1 🟡] [BUG]: TensorParallel vector move ctor/assign/swap are not noexcept

### Tier C 优先级分布: {'P3': 5, 'P2 🟢': 1, 'P1 🟡': 1}

## parameter (4 条: A=0 B=0 C=4)

### Tier C 优先级分布: {'P3': 4}

## record (4 条: A=1 B=0 C=3)

### Tier C 优先级分布: {'P3': 2, 'P2 🟢': 1}

## sysmo (4 条: A=0 B=1 C=3)

### Tier C 优先级分布: {'P2 🟢': 3}

## service (3 条: A=0 B=0 C=3)

### ⚠️ P0/P1 但内容空洞 (1 条，需优先补充)
  - [P1 🟡] [BUG] Service accessor get_global_resource() invoked from non-owning individual contexts - enforce c

### Tier C 优先级分布: {'P3': 2, 'P1 🟡': 1}

## statistics (4 条: A=1 B=1 C=2)

### Tier C 优先级分布: {'P3': 2}

## logger (3 条: A=1 B=0 C=2)

### Tier C 优先级分布: {'P2 🟢': 2}

## task (2 条: A=0 B=0 C=2)

### Tier C 优先级分布: {'P3': 2}

## transport (30 条: A=0 B=29 C=1)

### Tier C 优先级分布: {'P2 🟢': 1}

## blocker (2 条: A=1 B=0 C=1)

### Tier C 优先级分布: {'P2 🟢': 1}

## event (1 条: A=0 B=0 C=1)

### Tier C 优先级分布: {'P2 🟢': 1}

## mainboard (3 条: A=2 B=1 C=0)

## plugin_manager (2 条: A=2 B=0 C=0)

## service_discovery (2 条: A=1 B=1 C=0)