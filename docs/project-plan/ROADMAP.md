# pub/sub-loop 产品需求路线图

> 基于 1626 个 PRD 的全量数据分析 · 生成日期: 2026-07-10

---

## 一、项目全景概述

pub/sub-loop 是一个类区块链架构的世界模型个体系统。每个个体 (individual) 通过 DDS/RTPS 协议持续广播自身状态（位置、速度、属性），订阅者实时接收并融合为一致的世界快照。底层由 Cyber RT 提供协程调度与传输基础设施，展示层使用 needle-tools 在 Web 端渲染 GLB/3D 内容。

### 规模指标

| 维度 | 数据 |
|------|------|
| PRD 总数 | 1,626 |
| 模块数 | 26 |
| EPIC 数 | 54（其中 P0: 53） |
| FEA 数 | 457 |
| BUG 数 | 154 |
| 参与 Claude 实例 | 6 + 1 Manager |

### 优先级分布

| 优先级 | 数量 | 占比 | 说明 |
|--------|------|------|------|
| P0 🔴 | 56 | 3.4% | 系统核心 EPIC，阻塞性架构决策 |
| P1 🟡 | 158 | 9.7% | 关键 BUG（138 个）和高优需求 |
| P2 🟢 | 382 | 23.5% | 主力 FEA 交付层（361 个 FEA） |
| P3 | 1,030 | 63.3% | 长尾优化、文档、技术债 |

---

## 二、路线图阶段划分

基于当前 Sprint 分布和依赖关系，路线图划分为 4 个阶段。

### Phase 0: 架构奠基（当前 Sprint — 进行中）

**目标**: 完成核心传输统一、状态融合引擎、跨平台引导三大基座。

**Sprint 状态**: 467 items | In Progress: 219 | In Review: 202 | Blocked: 26

**关键交付物**:

| # | EPIC | 模块 | 负责人 | 状态 |
|---|------|------|--------|------|
| 1 | 统一 RTPS 与共享内存传输后端 | transport | Claude-D | In Progress |
| 2 | 多源个体状态融合引擎 | data | Claude-A | In Progress |
| 3 | 零拷贝异步个体状态传输协议 | transport | Claude-A | In Progress |
| 4 | 大规模世界状态缓冲 (100K+) | data | Claude-D | In Progress |
| 5 | 跨平台世界初始化容器 | mainboard | Claude-E | In Progress |
| 6 | Component 平台分级暴露与编译期适配 | component | Claude-E | In Progress |
| 7 | transport/memory_pool 路线图 | transport | Claude-A | In Progress |
| 8 | ChannelWriter pub/sub 调度优化 (Ampere/Hopper) | data | Claude-D | In Progress |
| 9 | 并发哈希表迁移到 DynamicComponentBase | component | Claude-E | In Progress |
| 10 | std 并行算法移植到 world::std | common | Claude-A | In Progress |
| 11 | Top-k 优先级选择框架 | data | Claude-D | In Progress |
| 12 | WSPRO warp-scan 调度算法产品化 | scheduler | Claude-D | In Progress |
| 13 | 世界查询算法补全 | data | Claude-D | In Review |

**里程碑**: 个体间端到端状态传输链路可运行，融合引擎原型就绪。

**退出标准**: 13 个 P0 EPIC 全部 Done，26 个 Blocked 项清零。

---

### Phase 1: 骨架搭建（Next Sprint）

**目标**: 将架构骨架扩展到全部 26 个模块，完成 P0 EPIC 剩余 40 个。

**Sprint 状态**: 172 items | Todo: 165 | Done: 1

**关键交付线**:

**传输与数据层强化**:
- 分离并消除 TensorParallel / PipelineParallel 冗余 (data, Claude-D)
- PipelineParallel 性能调优 (scheduler, Claude-D)
- Cyber RT 数据管道越界保护 (data, Claude-D)
- Replace Thrust/CUB types → libcu++ (data, Claude-D)

**计算基础扩展**:
- Extended Floating-Point 支持 (base, Claude-A)
- Hopper Cluster 支持 (base, Claude-A)
- Fork/Join Parallel Ranges (base, Claude-A)
- Atomics 改进 (base, Claude-A)
- mdspan-based 算法 (base, Claude-E)

**common 模块大规模移植** (14 个 EPIC):
- TensorParallel 大输入支持、SM120 调优、Device-scope 协作算法
- 工作窃取全面铺开、非确定性 reduce 利用、Segmented Scan 优化
- C++ Frontend for neuron_sp.c、TMA Exposure、异构序列范围
- 用户自定义调优 API、确定性算法

**工具链与 CI**:
- 第三方测试集成、CI 基准测试 MVP
- 文档体系重构 (diataxis)、维护者文档整合
- neuron::std::ranges 实现

**里程碑**: 全模块骨架代码就绪，核心 API surface 锁定。

---

### Phase 2: 实现填充（Skeleton → Implementation）

**目标**: 将 955 个 Skeleton 阶段 PRD 逐步实现为可测试代码。

**Sprint 状态**: 955 items (Skeleton) + 32 items (Implementation) | 主体 Todo

**工作分布**:

| 模块 | Skeleton 待实现 | 关键方向 |
|------|----------------|----------|
| common | ~300+ | 并行算法库完整实现、world::std 全集 |
| tools | ~250+ | CI/CD 管线、文档生成、测试框架 |
| io | ~120+ | 数据序列化、格式转换、流式 I/O |
| base | ~100+ | 内存管理、原子操作、浮点支持 |
| data | ~60+ | 状态缓冲、融合策略、查询引擎 |
| scheduler | ~40+ | 协程调度、工作窃取、管线并行 |

**里程碑**: 主路径功能可编译运行，集成测试覆盖 >60%。

---

### Phase 3: 质量收敛与发布准备

**目标**: 消灭 P1 BUG 存量、性能调优、文档完善。

**核心任务**:
- 消灭 138 个 P1 BUG（当前最大质量风险）
- 16 个 P3 BUG 分流处理
- 1,030 个 P3 项目分批纳入或标记 Won't Fix
- 性能基准建立与回归测试
- 面向 needle-tools 展示层的集成验证

---

## 三、模块路线图矩阵

按依赖拓扑排序，从底层到上层：

```
Phase 0 (当前)          Phase 1 (Next)           Phase 2 (实现)          Phase 3 (收敛)
─────────────────────────────────────────────────────────────────────────────────────────
base ──────────────────► FP/Hopper/Atomics ──────► 全量实现 ──────────────► 稳定化
  │
  ▼
transport ─── 统一后端 ─► memory_pool 完善 ────────► 零拷贝全链路 ─────────► 性能调优
  │
  ▼
data ──── 融合引擎 ─────► TP/PP 重构 ─────────────► 查询引擎完整 ─────────► 大规模验证
  │
  ▼
scheduler ── WSPRO ─────► PP 性能调优 ─────────────► 工作窃取全面 ─────────► 负载均衡
  │
  ▼
common ── std 移植 ──────► 14 个 EPIC 展开 ─────────► 300+ PRD 实现 ────────► API 冻结
  │
  ▼
component ── 平台适配 ──► PipelineParallel DL ────► 能力集完整 ──────────► 插件化
  │
  ▼
mainboard ── 引导脚本 ──►                          ► 跨平台验证 ──────────► 发布包
  │
  ▼
tools ──────────────────► CI/文档/测试 ──────────► 全自动化 ─────────────► 发布就绪
```

---

## 四、Claude 实例工作负载规划

| 实例 | 当前 Sprint | 全量 | 专长领域 | 负载评估 |
|------|------------|------|----------|----------|
| Claude-D | 133 | 546 | data/scheduler/common 核心算法 | 🔴 过载 — 建议分流 |
| Claude-E | 169 | 551 | component/tools/io 基础设施 | 🔴 过载 — 建议分流 |
| Claude-A | 82 | 293 | transport/base 底层协议 | 🟡 中等 |
| Claude-G | 37 | 106 | common 策略算法 | 🟢 正常 |
| Claude-B | 22 | 81 | 混合任务 | 🟢 可承接更多 |
| Claude-C | 13 | 38 | 确定性算法/前端 | 🟢 可承接更多 |

**建议**: Claude-D 和 Claude-E 当前各承载 130-170 项，远超均值。应将 common 模块 P3 项拆分给 Claude-B/C/G。

---

## 五、关键里程碑时间线

| 里程碑 | 阶段 | 前置条件 | 交付标准 |
|--------|------|----------|----------|
| M0: 传输统一 | Phase 0 | — | RTPS + SHM 统一抽象层可用 |
| M1: 融合引擎 Alpha | Phase 0 | M0 | LastWriteWins + WeightedAvg 策略可运行 |
| M2: 100K 个体压测 | Phase 0 | M0 + M1 | 背压 + 分块传递通过 100K 个体测试 |
| M3: 跨平台引导 | Phase 0 | — | x86 + ARM + RT-Thread 三平台启动成功 |
| M4: 骨架锁定 | Phase 1 | M0-M3 | 全部 53 P0 EPIC Done |
| M5: API 冻结 | Phase 2 | M4 | world::std 和 pub/sub API surface 不再变更 |
| M6: BUG 清零 | Phase 3 | M5 | P0/P1 BUG 全部关闭 |
| M7: needle-tools 集成 | Phase 3 | M5 | Web 端 GLB 渲染与实时状态同步验证 |
| M8: 1.0 Release | Phase 3 | M6 + M7 | 文档完整、CI 全绿、性能基准建立 |

---

## 六、风险摘要（详见 RISK_ASSESSMENT.md）

| 风险 | 等级 | 影响 |
|------|------|------|
| P1 BUG 积压 138 个 | 🔴 高 | 质量债务持续增长 |
| Claude-D/E 过载 | 🔴 高 | 交付瓶颈 |
| 26 项 Blocked | 🟡 中 | Phase 0 退出受阻 |
| Needs Triage 55 项 | 🟡 中 | 需求遗漏风险 |
| Phase 1 Skeleton 955 项 | 🟡 中 | 实现周期不可控 |
