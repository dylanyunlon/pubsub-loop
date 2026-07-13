# pub/sub-loop 项目完整上下文

## 项目性质
pub/sub-loop 是一个类区块链的世界模型个体系统。核心概念：
- "个体"(individual) 通过 DDS/RTPS 协议持续广播自身状态（位置、速度、属性）
- 订阅者实时接收并融合为一致的世界快照
- 底层由 Cyber RT 提供协程调度与传输基础设施
- 展示层使用 needle-tools (github.com/needle-tools) 在 Web 端渲染 GLB/3D 内容
- 展示只是其中一个模块,不是所有模块都属于展示

## 三层架构
- Layer 1 (世界模型层): transport, node, mainboard, record, message, context, logger, io, base, common, time, parameter, event
- Layer 2 (内部决策层): profiler, statistics, plugin_manager, blocker, scheduler, croutine, service_discovery, component, data, sysmo, task
- Layer 3 (展示层): needle-engine (GLB/3D web展示), 通过 StateBridge 连接 Layer 2 的 WorldView

## 规模
- 总PRD数: 1626
- 模块数: 26
- EPIC: ~54 | FEA: ~457 | BUG: ~154 | THEME: ~9
- 优先级: P0(56) | P1(158) | P2(382) | P3(1030)
- 状态: Todo(1120) | In Progress(219) | In Review(202) | Blocked(26) | Done(4)
- 参与Claude实例: Claude-A/B/C/D/E/F/G + Manager

## 自定义字段
- Status: Needs Triage | Todo | In Progress | In Review | Blocked | Done
- Priority: P0 🔴 | P1 🟡 | P2 🟢 | P3
- Sprint: Current | Next | Phase 1: Skeleton | Phase 2: Implementation | Phase 3: Integration
- Claude: Manager | Claude-A ~ Claude-G
- Language: C++/CUDA | Python | Mixed | LaTeX
- Module: 26个模块
- 还有: Exec Status, Commit Range, LOC Changed, Conv ID 等


## 模块PRD质量现状

模块                      总数   完整PRD    需细化   P0   P1
----------------------------------------------------
common                 494     113      8   20   45
tools                  374       0    243    7   36
io                     179       0      5    2   30
base                   173       1      0    5    9
data                    90       1      0    9    3
scheduler               61       1      0    2    6
component               41       0      0    3    8
profiler                39       0      0    0    4
transport               30      28      0    3    2
context                 26       0      0    0    5
time                    26       0      0    1    2
message                 22       0      0    0    2
croutine                14       0      0    0    1
node                    12       0      0    0    0
unassigned              11       1      5    3    4
statistics               4       1      0    0    0
record                   4       0      0    0    0
sysmo                    4       0      0    0    0
parameter                4       0      0    0    0
mainboard                3       0      0    1    0
logger                   3       0      0    0    0
service                  3       0      0    0    1
plugin_manager           2       0      0    0    0
service_discovery        2       1      0    0    0
blocker                  2       0      0    0    0
task                     2       0      0    0    0
event                    1       0      0    0    0


## 高质量PRD样例（供风格参考）

以下是项目中已有的高质量PRD,新生成的PRD应达到同等水平。

### 样例 1: [P0 🔴] [transport]
**标题**: [EPIC] Unify RTPS and shared-memory transport backends into a single individual-to-individual state delivery pipeline
**正文** (6943 chars):

## Description

@pub-sub-loop-transport opened · P0 🔴 · EPIC · Claude-D

The transport layer currently exposes two entirely separate code paths for moving individual state between participants in the world model:

1. **RTPS (Real-Time Publish-Subscribe)** — the DDS/RTPS wire protocol used for inter-process and inter-node individual-to-individual communication. Each individual publishes its position, velocity, orientation, and custom attribute data via RTPS `DataWriter`; subscribers on remote nodes receive state updates via `DataReader` with QoS-based filtering (deadline, liveliness, reliability).

2. **Shared-memory segment transport** — the intra-node fast path. When publisher and subscriber individuals reside in the same OS process or on the same NUMA node, state payloads are written into a lock-free shared-memory ring buffer (`transport::ShmSegment<IndividualState>`) and read via `transport::ShmReader`, bypassing serialization entirely.

These two backends share no common abstraction. A `component::IndividualDriver` that needs to publish state must today select a transport at init time via `transport::ParticipantConfig::backend_type`, and the selection is static for the lifetime of that individual's node. This creates three concrete problems:

### Problem 1: No runtime transport migration

When two individuals that were on separate nodes (using RTPS) get co-located onto the same node by the scheduler (e.g. after a world partition rebalance), their pub/sub channel continues to pay the RTPS serialization + UDP loopback cost. There is no mechanism to detect co-location and migrate the channel to shared-memory mid-session.

### Problem 2: Duplicated channel plumbing

`transport::RtpsWriter<T>` and `transport::ShmWriter<T>` share ~60% of their code (QoS enforcement, history cache, deadline tracking, sequence numbering) but are implemented as independent class hierarchies. Bug fixes in one backend routinely fail to be ported to the other.

### Problem 3: Multi-backend fan-out

An individual that needs to publish state to both local subscribers (shared-memory) and remote subscribers (RTPS) must today instantiate two writers and manually dispatch. The `data::ChannelWriter<T>` abstraction does not support multi-backend fan-out.

## Proposed Design

Introduce `transport::UnifiedWriter<T>` and `transport::UnifiedReader<T>` that sit above both backends:

```
┌─────────────────────────────────────────────────┐
│           transport::UnifiedWriter<T>           │
│  ┌──────────┐  ┌──────────┐  ┌───────────────┐ │
│  │ QoS      │  │ History  │  │ Sequence      │ │
│  │ Enforcer │  │ Cache    │  │ Numbering     │ │
│  └────┬─────┘  └────┬─────┘  └──────┬────────┘ │
│       └──────────────┼───────────────┘          │
│              ┌───────▼────────┐                 │
│              │ BackendRouter  │                 │
│              └───┬────────┬───┘                 │
│          ┌───────▼──┐ ┌───▼──────────┐          │
│          │ ShmSink  │ │  RtpsSink    │          │
│          └──────────┘ └──────────────┘          │
└─────────────────────────────────────────────────┘
```

### BackendRouter dispatch policy

`BackendRouter` maintains a routing table keyed by subscriber individual ID:

```cpp
struct RouteEntry {
    IndividualId subscriber;
    TransportBackend backend;     // Shm | Rtps | IntraProcess
    uint64_t topology_distance;   // hop count in world graph
    size_t last_payload_bytes;
    Timestamp last_migration_check;
};
```

On every `write(state)` call:
1. For each active subscriber, `BackendRouter::select_backend()` evaluates:
   - Is subscriber in-process? → `IntraProcess` (zero-copy pointer handoff)
   - Is subscriber on same NUMA node? → `Shm` (shared-memory segment)
   - Otherwise → `Rtps` (serialized wire protocol)
2. State payload is dispatched to the appropriate sink. If multiple backends are active (fan-out), serialization happens at most once (lazy).

### Runtime migration protocol

When `service_discovery` detects

... (截断)

---

### 样例 2: [P0 🔴] [transport]
**标题**: [EPIC] Zero-copy async individual state transfer protocol - bulk position and data sync between individuals without intermediate buffering
**正文** (8280 chars):

## Description

@pub-sub-loop-transport opened · P0 🔴 · EPIC · Claude-A

Individual state transfers in the pub/sub-loop world model are currently synchronous and buffered. When individual A publishes a 2KB position+attribute state update, the transport layer today performs:

1. Serialize `IndividualState` → flat buffer (memcpy #1)
2. Copy flat buffer into `transport::HistoryCache` ring slot (memcpy #2)
3. Copy from history cache into backend-specific send buffer — RTPS socket buffer or Shm segment (memcpy #3)
4. On the receiver side, copy from receive buffer into `data::ChannelBuffer` (memcpy #4)

That is **4 memcpy operations** for a single state update. For a world with 10,000 active individuals publishing at 30Hz, this is 1.2 million unnecessary copies per second. The world model's latency budget for individual state propagation is 3ms end-to-end; the current pipeline burns ~1.8ms in copies alone on large payloads (>64KB, e.g. individuals carrying mesh geometry or point-cloud sensor data).

### Current hot path (profiler trace)

```
individual::publish(state)
  → message::serialize(state, &flat_buf)          // 180µs for 64KB payload
    → transport::ShmWriter::write(flat_buf)
      → memcpy(history_slot, flat_buf, len)       // 45µs
      → memcpy(shm_segment + offset, flat_buf, len) // 45µs
        → subscriber notified via eventfd
          → transport::ShmReader::read()
            → memcpy(channel_buf, shm_ptr, len)   // 45µs
              → message::deserialize(channel_buf, &state) // 180µs
```

Total: ~495µs per 64KB state update (intra-node, shared memory). RTPS path is worse due to UDP serialization overhead.

## Proposed Design

### Zero-copy state publishing

Introduce `transport::ZeroCopyRegion` — a memory-mapped region that both writer and reader can access directly:

```cpp
namespace transport {

// Allocate a state slot from the zero-copy region
// Returns a handle that the publisher writes into directly
template <typename IndividualStateT>
class ZeroCopyPublisher {
public:
    // Acquire a writable slot — the individual writes its state directly here
    // No intermediate buffer, no serialization for intra-node
    StateSlotHandle acquire_slot();
    
    // Commit the slot — makes it visible to all subscribers atomically
    // Uses release-store on the slot's sequence counter
    void commit(StateSlotHandle handle);
    
    // For RTPS (inter-node): serialize only when a remote subscriber exists
    // Lazy serialization — only triggered when BackendRouter has RTPS routes
    void commit_with_remote_sync(StateSlotHandle handle);
};

// Zero-copy state region layout in shared memory
// 
// ┌──────────────────────────────────────────────────┐
// │  RegionHeader (64B, cache-line aligned)          │
// │  ┌─────────────┬────────────┬──────────────────┐ │
// │  │ write_seq   │ read_seq   │ num_slots        │ │
// │  └─────────────┴────────────┴──────────────────┘ │
// ├──────────────────────────────────────────────────┤
// │  Slot[0]: SeqNo(8B) + StatePayload(aligned)     │
// │  Slot[1]: SeqNo(8B) + StatePayload(aligned)     │
// │  ...                                             │
// │  Slot[N-1]: SeqNo(8B) + StatePayload(aligned)   │
// └──────────────────────────────────────────────────┘
//
// Publisher: acquire_slot() → write state in-place → commit()
// Subscriber: poll read_seq, read slot directly, no copy

struct ZeroCopyRegion {
    static constexpr size_t CACHE_LINE = 64;
    
    struct alignas(CACHE_LINE) Header {
        std::atomic<uint64_t> write_sequence;
        std::atomic<uint64_t> min_read_sequence;  // slowest subscriber
        uint32_t num_slots;
        uint32_t slot_stride;  // bytes per slot, padded to cache line
    };
    
    Header header;
    // Slots follow immediately, each cache-line aligned
    // slot_ptr(i) = base + sizeof(Header) + i * slot_stride
};

}  // namespace transport
```

### Subscriber zero-copy read path

```cpp
template <typename IndividualStateT>
class ZeroC

... (截断)

---


## 当前最需要补充的PRD (P0/P1, body < 500 chars)

- [P1 🟡] [tools] [Next] CI: Test conda-based workflows
- [P1 🟡] [tools] [Next] Document `neuron.core` vs "Neuron cores"
- [P1 🟡] [tools] [Next] [DOC]: Keep unsupported APIs listed in a standalone page
- [P1 🟡] [tools] [Next] Further modernization of `neuron.bindings`'s build backend
- [P0 🔴] [tools] [Next] [EPIC] Third-party testing in CI
- [P0 🔴] [tools] [Next] [EPIC] CI benchmarking MVP
- [P0 🔴] [tools] [Next] [EPIC]: Improve documentation content by adopting diataxis
- [P0 🔴] [tools] [Next] [EPIC] Consolidate all maintainer process docs in new "Maintainer Docs" page
- [P0 🔴] [tools] [Next] [EPIC] Docs Overhaul
- [P0 🔴] [tools] [Next] [EPIC]: Implement `<neuron/std/ranges>` and associated headers
- [P1 🟡] [] [Current] [L3-005] WebSocket 连接管理：重连 · 帧同步 · 背压