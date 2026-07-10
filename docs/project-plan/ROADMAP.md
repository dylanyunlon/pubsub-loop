# pub/sub-loop 产品需求路线图

> 生成时间：2026-07-10 | 数据源：1626 PRDs from GitHub Project #4

---

## 一、项目全景

pub/sub-loop 是一个类区块链架构的世界模型个体系统。每个**个体（Individual）**通过 DDS/RTPS 协议持续广播自身状态（位置、速度、属性），订阅者实时接收并融合为一致的世界快照。Cyber RT 提供协程调度与传输基础设施，needle-tools 在 Web 端渲染个体动态（GLB/3D）。

**当前规模**：1626 条产品需求，覆盖 26 个模块，由 6 个 Claude Agent 并行执行。

| 指标 | 数值 |
|------|------|
| 总 PRD | 1626 |
| EPIC | 54 |
| FEA | 457 |
| BUG | 154 |
| 完成率 | 0.25%（4/1626 Done） |
| 阻塞项 | 26 |
| 待分诊 | 55 |

---

## 二、路线图总览

路线图划分为四个阶段，对应项目已有的 Sprint 标签，并补充时间建议。

```
Phase 0: 解除阻塞 & 分诊              ← 立即（Week 1-2）
Phase 1: Skeleton 骨架搭建            ← 955 items, ~12 sprints
  ├── 1A: 核心传输 & 数据管道           ← Sprint 1-3
  ├── 1B: 调度 & 协程基础              ← Sprint 2-4
  ├── 1C: 公共算法库 & IO              ← Sprint 3-8
  └── 1D: 工具链 & 文档                ← Sprint 4-12（持续）
Phase 2: Implementation 实现          ← 32 items + 新增
Phase 3: Optimization & Scale         ← 100K+ 个体、性能调优
```

---

## 三、Phase 0 — 紧急事项（Week 1-2）

### 3.1 解除 26 个 Blocked 项

当前 26 个条目处于 Blocked 状态，集中在 Current Sprint，直接拖慢进度。

| 分类 | 数量 | 典型阻塞原因 |
|------|------|-------------|
| P1 BUG | 6 | 编译错误、算法正确性 |
| P3 基础设施 | 12 | CI 脚本、文档链接、原子迁移 |
| P2 工具 | 3 | 文档预览、CI sanitizer |
| P3 Common 算法 | 5 | 依赖上游 libcu++/Neuron_SP 发布 |

**行动**：逐项分诊，区分"等待外部依赖"（标注 on-hold）和"可内部解决"（分配 Claude-D/E 立即处理）。

### 3.2 分诊 55 个 Needs Triage 项

55 个条目尚未分诊，其中 transport 模块占 21 个（全部 Needs Triage），需要立即确认优先级和 Sprint 归属。

---

## 四、Phase 1A — 核心传输 & 数据管道（Sprint 1-3）

这是整个世界模型的基础：个体发布状态 → 传输 → 订阅者融合。无此管道，上层模块无法运行。

### 关键 EPIC（P0）

| EPIC | 模块 | 状态 | 依赖 |
|------|------|------|------|
| 统一 RTPS + 共享内存传输后端 | transport | In Progress | base, scheduler, node |
| 零拷贝异步个体状态传输协议 | transport | In Progress | base, data, message |
| transport/memory_pool 路线图 | transport | In Progress | base, scheduler |
| 多源个体状态融合引擎 | data | In Progress | io, task |
| 大规模世界缓冲（100K+ 个体背压） | data | In Progress | transport, time |
| data::ChannelWriter 优化 | data | In Progress | scheduler |
| 缺失的世界查询算法 | data | In Review | tools, profiler |
| top-k 优先选择框架 | data | In Progress | profiler, message |

### 交付里程碑

- **M1（Sprint 1 末）**：统一传输抽象层 `transport::IndividualChannel<T>` 可编译、单元测试通过
- **M2（Sprint 2 末）**：零拷贝 + 共享内存路径端到端打通，benchmark 对比旧路径
- **M3（Sprint 3 末）**：融合引擎 + ChannelWriter 优化集成，100K 个体压力测试通过

---

## 五、Phase 1B — 调度 & 协程基础（Sprint 2-4）

调度器决定个体状态更新的执行时序，协程是并发的基本单元。

### 关键 EPIC（P0）

| EPIC | 模块 | 状态 |
|------|------|------|
| WSPRO warp-scan 算法产品化 | scheduler | In Progress |
| PipelineParallel 性能调优 | scheduler | Todo |
| 跨平台世界初始化容器 | mainboard | In Progress |

### 支撑项

- croutine 模块 14 项中 11 项 In Progress，进展健康
- mainboard 3 项全部 In Progress，需确保跨平台引导脚本覆盖 x86/ARM/RT-Thread

---

## 六、Phase 1C — 公共算法库 & IO（Sprint 3-8）

common（494 项）和 io（179 项）是体量最大的两个模块，承载世界模型的算法原语和高吞吐 I/O。

### 关键 EPIC（P0）

| EPIC | 模块 | 当前状态 |
|------|------|---------|
| Port std 等价并行算法到 world::std | common | In Progress |
| TensorParallel 大输入支持 | common | Todo |
| C++ Frontend for neuron_sp.c | common | Todo |
| 确定性算法 | common | Todo |
| 设备级协作算法 | common | Todo |
| Neuron_SP headers host-only 支持 | io | Todo |
| Python 暴露 neuron::ptx | io | Todo |

### 策略

common 模块 322 项在 Phase 1: Skeleton，大部分是 P3 骨架条目。建议按以下顺序推进：

1. 先完成 Current Sprint 的 110 项（42 In Progress + 56 In Review + 12 Blocked）
2. 优先推进 P0 EPIC 关联的 FEA
3. P3 骨架条目可延后到 Phase 2 批量处理

---

## 七、Phase 1D — 工具链 & 文档（Sprint 4-12，持续）

tools（374 项）覆盖 CI/CD、文档、代码生成、PR 审查自动化。

### 关键 EPIC（P0）

| EPIC | 状态 |
|------|------|
| 第三方 CI 测试 | Todo |
| CI 基准测试 MVP | Todo |
| 文档采用 Diataxis 框架 | Todo |
| 维护者文档合并 | Todo |
| 文档大修 | Needs Triage |
| `<neuron/std/ranges>` 实现 | Needs Triage |
| CI compute-sanitizer 修复 | Needs Triage |

### 策略

工具链不阻塞核心功能开发，但 CI 质量直接影响迭代速度。建议 Sprint 4 开始并行推进，每个 Sprint 分配 15-20% 产能给 tools。

---

## 八、Phase 2 — 实现阶段

当前仅 32 项标记为 Phase 2: Implementation（29 Needs Triage, 3 Done）。随着 Phase 1 骨架完成，大量 Phase 1 条目将升级进入 Phase 2。

### 预期新增内容

| 领域 | 预估条目 | 依赖 |
|------|---------|------|
| State Bridge（L3-001）：data::WorldView → WebSocket DeltaFrame | 新增 | Phase 1A 完成 |
| DeltaFrame 二进制协议（L3-002） | 新增 | State Bridge |
| needle-tools 渲染集成 | 新增 | DeltaFrame 协议 |
| 端到端个体生命周期测试 | 新增 | transport + data + scheduler |

---

## 九、Phase 3 — 优化 & 规模化

面向 100K+ 个体世界的性能优化。

| 目标 | 相关 EPIC |
|------|----------|
| 100K 个体稳定 60fps 状态同步 | 大规模世界缓冲、零拷贝传输 |
| 跨节点延迟 < 5ms | 统一传输后端、运行时迁移 |
| GPU 显存控制 < 4GB | memory_pool 路线图、top-k 筛选 |
| Web 端流畅渲染 | State Bridge + needle-tools |

---

## 十、Sprint 分配建议

| Sprint | 主题 | P0 | P1 | P2/P3 | 预估总量 |
|--------|------|----|----|-------|---------|
| S0（当前） | 解除阻塞 + 分诊 | 解除 26 blocked | 分诊 55 items | - | ~80 |
| S1 | 传输统一 + 融合引擎 | 5 EPIC 推进 | 20 FEA | 10 | ~35 |
| S2 | 零拷贝 + 调度器 | 4 EPIC 推进 | 15 FEA | 20 | ~40 |
| S3 | ChannelWriter + 世界查询 | 3 EPIC 收尾 | 25 FEA | 30 | ~60 |
| S4-6 | 算法库 + 工具链 | 10 EPIC | 40 FEA | 100 | ~150 |
| S7-12 | 骨架批量 + 文档 | 5 EPIC | 30 FEA | 400 | ~435 |

---

## 十一、Claude Agent 工作分配现状与建议

| Agent | 当前负载 | In Progress | Blocked | 建议 |
|-------|---------|-------------|---------|------|
| Claude-E | 551 项 | 78 | 10 | 负载最重，10 个 blocked 需优先处理 |
| Claude-D | 546 项 | 70 | 9 | 9 个 blocked 集中在 common，需外部依赖确认 |
| Claude-A | 293 项 | 35 | 2 | 相对健康，可接收 Claude-E/D 溢出任务 |
| Claude-G | 106 项 | 18 | 2 | 产能有余，建议接手 io 模块 P0 EPIC |
| Claude-B | 81 项 | 10 | 3 | 聚焦 common/context |
| Claude-C | 38 项 | 7 | 0 | 最轻负载，建议接手 tools 文档/CI 工作 |

---

## 十二、关键度量 & 看板建议

| 度量 | 当前值 | Sprint 1 目标 | Phase 1 结束目标 |
|------|--------|-------------|----------------|
| Done 完成数 | 4 | 40 | 600 |
| Blocked 数 | 26 | 5 | 0 |
| Needs Triage | 55 | 0 | 0 |
| P0 完成率 | 0% | 25% | 100% |
| P1 BUG 修复率 | 0% | 50% | 100% |
