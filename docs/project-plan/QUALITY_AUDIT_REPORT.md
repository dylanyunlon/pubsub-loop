# pub/sub-loop PRD 质量审计报告

> 审计时间: 2026-07-13 | 数据源: GitHub Project #4 全量 1,631 条

## 一、核心发现

### 1,631 条 PRD 的真实质量分层

| 层级 | 数量 | 占比 | 定义 |
|------|------|------|------|
| **Tier A — 完整 PRD** | 58 | 3.6% | 有架构定位、问题陈述、个体交互模型、API 设计、展示层影响 |
| **Tier B — 部分内容** | 221 | 13.5% | 有部分技术实质，但缺少系统性框架 |
| **Tier C — 模板套出** | 1,352 | 82.9% | 五个准入问题被同一模板句绕过 |

### 模板化的铁证

994 条 PRD 包含"个体输出动态关联"section，但内容是**每个模块一句话、全模块复用**：

| 模块 | PRD数 | 模板种类数 | 模板句内容 |
|------|-------|-----------|-----------|
| common | 284 | 1 | "common 模块的算法直接决定个体状态的计算精度和处理吞吐量..." |
| io | 172 | 1 | "io 模块的处理效率直接决定大规模世界（100K+ 个体）的状态更新频率..." |
| base | 160 | 1 | "base 模块的对齐和原子操作保障了个体状态在 pub/sub 传输中的数据完整性..." |
| tools | 84 | 1 | "tools 保障了从源码到 GLB 动态输出的完整交付链路..." |
| data | 74 | 1 | "data 模块将多个个体的原始状态数据融合为一致的世界视图..." |
| scheduler | 47 | 1 | "scheduler 的 tick-loop 节奏直接决定个体位置/属性更新的帧率..." |
| 其余 15 个模块 | 173 | 各1 | 每个模块一句固定描述 |

**每一种模板句在其模块内重复次数 = 模块 PRD 总数。** 没有任何逐条定制。

### 五个准入问题的回答率

| 问题 | 应答数/总数 | 比例 |
|------|-----------|------|
| Q1: 架构定位 (在三层中的位置) | 15/1631 | 0.9% |
| Q2: 问题陈述 (解决什么问题) | 106/1631 | 6.5% |
| Q3: 个体交互模型 (个体间如何通信) | 72/1631 | 4.4% |
| Q4: API + 设计方案 (当前→目标) | 102/1631 | 6.3% |
| Q5: 展示层影响 (对 Needle/GLB 的影响) | 806/1631* | 49.4%* |
| **五项全部回答** | **10/1631** | **0.6%** |

*Q5 的 806 条中，大部分是模板句而非真正的影响分析

### 五项全部回答的 10 条 PRD

1. [EPIC] Setup cross-platform world initialization (mainboard)
2. [FEA] Integrate distribution-based statistical comparison (profiler)
3. [FEA] Declare inter-individual dependency graph (mainboard)
4. [FEA] Bulk-process world state snapshots (statistics)
5. [FEA] Enforce fully-qualified individual-type names (plugin_manager)
6. [FEA] Record模块个体状态快照导出 (record)
7. [FEA] PluginManager新增find_individual_plugin_path (plugin_manager)
8. [FEA] Logger模块实现std::format风格 (logger)
9. [FEA] service_discovery: diff_by_role() (service_discovery)
10. [FEA] blocker: refactor topology.h (blocker)

## 二、Tier C 的结构模式

典型 Tier C PRD body 结构 (~800-1300 字符):

```
## Description
[1-3 句功能描述 + 代码片段(可选)]

确保个体（individual）在世界中持续更新数据和位置时，相关功能正确、高效、可靠。

### 个体输出动态关联
[模块级模板句，与具体 PRD 无关]

## Dependencies
- [1-3 条泛化依赖]

## Sub-tasks
- [ ] [3-6 条子任务，通常合理但缺少优先级和工时]

## Acceptance Criteria
- [ ] [3-5 条验收标准，通常合理但缺少量化指标]
```

**缺什么:** 没有问题陈述 (为什么需要这个)、没有当前 API 分析、没有 Before/After 代码对比、没有架构定位、没有依赖链路分析。

## 三、行动计划

### 优先级排序: 先修 P0/P1，再修 P2/P3

| 批次 | 范围 | 数量 | 说明 |
|------|------|------|------|
| Batch 0 | P0 Tier C | ~40 | 最高优先级但内容空洞的 PRD |
| Batch 1 | P1 Tier C | ~130 | 次高优先级 |
| Batch 2 | Current Sprint Tier C | ~350 | 当前冲刺中的模板项 |
| Batch 3 | Phase 1 Skeleton Tier C | ~800+ | 骨架阶段的大批量项 |

### 每条 PRD 需要补充的内容

对标 NVIDIA/cccl 的 Issue 质量 (参考: [CUDAX][CUCO] Use cuda::launch #9656):

1. **架构定位** (1-2 句): 这个功能在三层架构中的位置
2. **问题陈述** (3-5 段): 当前行为是什么、为什么有问题、影响范围
3. **当前代码路径** (代码块): 现有 API / 代码示例
4. **修改后代码路径** (代码块): Before/After 对比
5. **个体交互影响** (1 段): 这个改动如何影响个体间的 pub/sub 通信
6. **展示层影响** (1 段): 具体到这个 PRD，对 Needle/GLB 渲染的影响是什么
7. **依赖链** (表格): Depends on / Blocks / Related
8. **实施步骤** (带优先级): P0/P1/P2 标记
9. **验收标准** (可量化): 延迟 < Xms, 吞吐 > Y/s, 测试覆盖率等
