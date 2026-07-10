# pub/sub-loop Sprint 计划

> 基于 1626 个 PRD 全量分析 · 生成日期: 2026-07-10

---

## 一、当前 Sprint 状态仪表板

### 整体进度

| 指标 | 数值 | 健康度 |
|------|------|--------|
| Sprint 总项数 | 467 | — |
| In Progress | 219 (46.9%) | 🟡 并行度过高 |
| In Review | 202 (43.3%) | 🔴 Review 堆积 |
| Blocked | 26 (5.6%) | 🟡 需立即处理 |
| Todo | 10 (2.1%) | 🟢 基本拉入 |
| Needs Triage | 10 (2.1%) | 🟡 应为 0 |
| Done | 0 (0%) | 🔴 无交付产出 |

### 关键发现

**Review 瓶颈**: 202 项处于 In Review 状态，占 Sprint 的 43.3%。这意味着近半数工作已完成开发但卡在 Review 环节。按 Claude 实例分布：Claude-A 41 项、Claude-D 52 项、Claude-E 80 项在 Review 中。

**零 Done**: 当前 Sprint 没有任何一项标记为 Done。这是最大的红旗信号——大量工作在流动但没有完成闭环。

**建议**: 立即设定 "Review → Done" 转化目标，本周内至少将 50 个 In Review 项推到 Done。

---

## 二、当前 Sprint 按模块分解

### P0 EPIC 追踪板

| # | EPIC | 模块 | 负责人 | 状态 | 阻塞项 |
|---|------|------|--------|------|--------|
| 1 | 统一 RTPS + SHM 传输后端 | transport | Claude-D | In Progress | 依赖 memory_pool |
| 2 | 多源个体状态融合引擎 | data | Claude-A | In Progress | — |
| 3 | 零拷贝异步个体状态传输 | transport | Claude-A | In Progress | 依赖 EPIC #1 |
| 4 | 大规模世界状态缓冲 100K+ | data | Claude-D | In Progress | 依赖 EPIC #1,#3 |
| 5 | 跨平台世界初始化容器 | mainboard | Claude-E | In Progress | — |
| 6 | Component 平台分级暴露 | component | Claude-E | In Progress | — |
| 7 | transport/memory_pool 路线图 | transport | Claude-A | In Progress | — |
| 8 | ChannelWriter pub/sub 调度优化 | data | Claude-D | In Progress | 依赖 EPIC #12 |
| 9 | 并发哈希表 → DynamicComponentBase | component | Claude-E | In Progress | — |
| 10 | std 并行算法 → world::std | common | Claude-A | In Progress | — |
| 11 | Top-k 优先级选择框架 | data | Claude-D | In Progress | 依赖 EPIC #2 |
| 12 | WSPRO warp-scan 调度产品化 | scheduler | Claude-D | In Progress | — |
| 13 | 世界查询算法补全 | data | Claude-D | In Review | — |

### 建议完成顺序

根据依赖关系，推荐的 EPIC 完成顺序为：

```
Wave 1 (无前置依赖，立即推进):
  #7  transport/memory_pool ─────────┐
  #5  跨平台世界初始化容器            │
  #6  Component 平台分级暴露          │
  #10 std 并行算法 → world::std      │
  #12 WSPRO warp-scan 调度产品化     │
  #13 世界查询算法补全 (已 In Review) │
                                     │
Wave 2 (依赖 Wave 1):               │
  #1  统一 RTPS + SHM ◄─────────────┘ (依赖 #7)
  #2  多源状态融合引擎
  #9  并发哈希表迁移
  #8  ChannelWriter 调度优化 (依赖 #12)

Wave 3 (依赖 Wave 2):
  #3  零拷贝异步传输 (依赖 #1)
  #11 Top-k 优先级选择 (依赖 #2)

Wave 4 (依赖 Wave 3):
  #4  大规模世界状态缓冲 (依赖 #1, #3)
```

---

## 三、Claude 实例 Sprint 任务分配

### 当前分配

| 实例 | 总项 | In Progress | In Review | Blocked | 负载 |
|------|------|------------|-----------|---------|------|
| Claude-D | 133 | 70 | 52 | 9 | 🔴 |
| Claude-E | 169 | 78 | 80 | 10 | 🔴 |
| Claude-A | 82 | 35 | 41 | 2 | 🟡 |
| Claude-G | 37 | 18 | 16 | 2 | 🟢 |
| Claude-B | 22 | 10 | 9 | 3 | 🟢 |
| Claude-C | 13 | 7 | 4 | 0 | 🟢 |
| Manager | 11 | 1 | 0 | 0 | 🟢 |

### 建议重分配

| 变更 | 来源 | 目标 | 项数 | 说明 |
|------|------|------|------|------|
| common P3 FEA | Claude-D | Claude-G | ~30 | Claude-G 已有 common 经验 |
| common P3 PRD | Claude-D | Claude-B | ~20 | 释放 Claude-D 算法产能 |
| tools P3 FEA | Claude-E | Claude-C | ~25 | Claude-C 可承接工具链 |
| tools P3 DOC | Claude-E | Claude-B | ~20 | 文档任务不需要深度领域知识 |
| Needs Triage | 散落 | Manager | 10 | Manager 应接管全部 Triage |

重分配后预期负载：

| 实例 | 调整后 | 负载 |
|------|--------|------|
| Claude-D | ~83 | 🟡 可控 |
| Claude-E | ~124 | 🟡 偏高但可接受 |
| Claude-A | 82 | 🟡 不变 |
| Claude-G | ~67 | 🟡 适中 |
| Claude-B | ~62 | 🟡 适中 |
| Claude-C | ~38 | 🟢 正常 |
| Manager | ~21 | 🟢 含 Triage 职责 |

---

## 四、Blocked 项解除计划

### 阻塞链分析

```
[Blocked] base: pool_memory_resource 可行性评估
    │
    ├──► [Blocked] transport: memory_pool 相关项
    │        │
    │        └──► [间接受阻] data: 大规模缓冲相关项
    │
    └──► [Blocked] base: DistributedCore atomics 迁移

[Blocked] common: std::string + tensor_parallel::tuple 编译失败
    │
    └──► [Blocked] common: min_element/max_element int_max 问题

[Blocked] io: NVTX v2 + PipelineParallel 编译冲突
    │
    └──► [Blocked] io: 相关序列化功能

[Blocked] tools: 文档链接/API doc 问题 (独立，不阻塞核心)
```

### 解除顺序和时间目标

| 批次 | 阻塞项 | 优先级 | 目标时间 | 负责人 |
|------|--------|--------|---------|--------|
| Batch 1 | base pool_memory_resource Spike | P1 → P0 | 本周 | Claude-A |
| Batch 1 | common string+tuple 编译修复 | P1 → P0 | 本周 | Claude-A |
| Batch 2 | io NVTX v2 编译冲突 | P1 | 下周 | Claude-E |
| Batch 2 | base atomics 迁移评估 | P3 | 下周 | Claude-A |
| Batch 3 | tools 文档修复 | P3 | Phase 0 结束前 | Claude-B |

---

## 五、Next Sprint 预规划

### 容量规划

Next Sprint 包含 172 items，其中 165 Todo、1 Done。

按类型分布（预计）：
- EPIC: 40 个 P0 EPIC（Phase 0 遗留 + Next 原生）
- FEA: 大量 P2 FEA 将从 Phase 1 Skeleton 提前拉入
- BUG: 应从 138 个 P1 BUG 中拉入一批

### 建议 Sprint 目标

**目标 1: P0 EPIC 推进** — 完成 Next Sprint 的 40 个 P0 EPIC 的设计评审和初始实现。

关键 EPIC 分组：

**Group A: 数据层重构** (Claude-D 主导)
- TP/PP 冗余消除
- PP 性能调优
- Thrust/CUB → libcu++
- 数据管道越界保护

**Group B: 计算基础** (Claude-A 主导)
- Extended Floating-Point
- Hopper Cluster
- Fork/Join Ranges
- Atomics 改进

**Group C: common 算法扩展** (全员参与)
- 14 个 common EPIC 按子领域分配到 6 个 Claude 实例
- 每实例 2-3 个 EPIC，避免单点瓶颈

**Group D: 工具链** (Claude-E + Claude-C)
- CI 基准测试 MVP
- 第三方测试集成
- 文档重构

**目标 2: BUG 消灭** — 从 138 个 P1 BUG 中消灭至少 30 个。

**目标 3: Triage 清零** — 55 个 Needs Triage 项全部分类完毕。

---

## 六、Sprint 度量指标

### 建议追踪的 KPI

| 指标 | 当前值 | 目标值 | 说明 |
|------|--------|--------|------|
| Sprint Done 率 | 0% | >30% | 必须有产出闭环 |
| Review → Done 转化率 | 0/202 | >50% | 打通 Review 瓶颈 |
| Blocked 项数 | 26 | 0 | Phase 0 退出标准 |
| Needs Triage 项数 | 55 | 0 | 需求管理健康度 |
| P1 BUG 存量 | 138 | <110 | 每 Sprint 消灭 20%+ |
| Claude 实例负载标准差 | 极高 | 降低 50% | 均衡分配 |

### Sprint 回顾检查清单

每个 Sprint 结束时应回答：
1. 有多少项从 In Review 流转到 Done？阻塞 Review 的原因是什么？
2. 新增 Blocked 项是否超过解除的 Blocked 项？
3. 是否有新的 EPIC 级需求产生？是否影响路线图？
4. Claude 实例间的工作量差异是否在收窄？
5. 是否有 Needs Triage 项超过 2 周未处理？

---

## 七、里程碑检查点

| 检查点 | 时机 | 通过标准 |
|--------|------|---------|
| CP0: Phase 0 退出 | 当前 Sprint 结束 | 13 个 EPIC Done, 0 Blocked, Review 堆积 <50 |
| CP1: Phase 1 中期 | Next Sprint 中间 | 20 个 EPIC In Progress, BUG 消灭 30+, Triage 清零 |
| CP1.5: Phase 1 退出 | Next Sprint 结束 | 40 个 EPIC Done 或 In Review, API surface 草案冻结 |
| CP2: Skeleton 分批 | Phase 2 启动前 | 955 项分为 ≤4 批次, 每批有独立里程碑 |
| CP3: 集成验证 | Phase 3 启动时 | 端到端 pub/sub 通路验证, needle-tools 原型 |
