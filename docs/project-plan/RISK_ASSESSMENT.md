# pub/sub-loop 风险评估报告

> 基于 1626 个 PRD 全量分析 · 生成日期: 2026-07-10

---

## 风险总览

| ID | 风险 | 等级 | 概率 | 影响 | 当前状态 |
|----|------|------|------|------|----------|
| R1 | P1 BUG 积压失控 | 🔴 高 | 已发生 | 质量/进度 | 未缓解 |
| R2 | Claude-D/E 工作负载过载 | 🔴 高 | 已发生 | 进度 | 未缓解 |
| R3 | transport 模块枢纽风险 | 🔴 高 | 高 | 架构 | 进行中 |
| R4 | Blocked 项级联阻塞 | 🟡 中 | 已发生 | 进度 | 部分缓解 |
| R5 | Needs Triage 需求黑洞 | 🟡 中 | 高 | 范围 | 未缓解 |
| R6 | Skeleton 阶段实现估算不足 | 🟡 中 | 高 | 进度 | 未评估 |
| R7 | 跨平台兼容性 | 🟡 中 | 中 | 技术 | 进行中 |
| R8 | API 不稳定导致返工 | 🟡 中 | 中 | 进度/质量 | 未缓解 |
| R9 | needle-tools 集成风险 | 🟢 低 | 中 | 交付 | 未启动 |
| R10 | 文档债务 | 🟢 低 | 已发生 | 可维护性 | 部分缓解 |

---

## 风险详细分析

### R1: P1 BUG 积压失控 🔴

**现状**: 138 个 P1 BUG 全部处于 Current Sprint，但无一进入 Done。

**数据佐证**:
- P1 BUG 总数: 138（占全部 BUG 的 89.6%）
- 分布模块: common (最多)、tools、io、base、data
- 当前 Sprint 完成率: 0/138 = 0%
- P0 BUG: 0 个（说明 BUG 都在 P1 堆积，未做二次分级）

**影响分析**:
- 技术债每个 Sprint 持续累积，修复成本随代码变更线性增长
- BUG 可能阻塞 Phase 2 实现阶段的模块集成
- 部分 BUG 涉及编译错误（如 `std::string + tensor_parallel::tuple` 编译失败），直接阻塞开发

**缓解建议**:
1. 对 138 个 P1 BUG 做二次分级：将编译阻塞类提升至 P0，将文档类降级至 P2
2. 每 Sprint 固定 20% 产能用于 BUG 修复（约 90 项/Sprint 的 18 项）
3. 指定 Claude-B（当前仅 22 项）为 BUG 消灭专责实例

---

### R2: Claude-D/E 工作负载过载 🔴

**现状**: 两个实例承载了全量 PRD 的 67.5%。

**数据佐证**:

| 实例 | 当前 Sprint | 全量 | 人均期望 (均分) | 超出倍数 |
|------|------------|------|----------------|---------|
| Claude-D | 133 | 546 | 232 | 2.35x |
| Claude-E | 169 | 551 | 232 | 2.37x |
| Claude-A | 82 | 293 | 232 | 1.26x |
| Claude-G | 37 | 106 | 232 | 0.46x |
| Claude-B | 22 | 81 | 232 | 0.35x |
| Claude-C | 13 | 38 | 232 | 0.16x |

**影响分析**:
- Claude-D 同时负责 data、scheduler、common 三个核心模块的算法 EPIC，任一阻塞即影响关键路径
- Claude-E 同时负责 component、tools、io、base 四大模块，跨模块上下文切换成本高
- Claude-B/C/G 共承载 225 项（13.8%），严重闲置

**缓解建议**:
1. 将 Claude-D 的 common 模块 P3 项（~200 项）分配给 Claude-G 和 Claude-B
2. 将 Claude-E 的 tools 模块 P3 项（~150 项）分配给 Claude-C 和 Claude-B
3. 建立跨实例 Review 机制，避免单点知识孤岛

---

### R3: transport 模块枢纽风险 🔴

**现状**: transport 是系统耦合度最高的模块，引用 7 个其他模块，被 5 个模块引用。

**数据佐证**:
- transport → data: 7 次跨模块引用
- transport → scheduler: 6 次
- transport → message: 5 次
- transport → component: 4 次
- transport → profiler: 3 次
- 3 个 P0 EPIC 同时进行中，任一延期影响全系统

**关键依赖链**:
```
transport memory_pool → transport 统一后端 → transport 零拷贝 → data 100K 缓冲
```
此链条中任一节点延期，后续全部级联延期。

**缓解建议**:
1. transport 的 3 个 P0 EPIC 必须明确完成顺序（memory_pool → 统一后端 → 零拷贝）
2. 定义 transport 抽象层接口并冻结，允许 data/scheduler 模块基于接口并行开发
3. 为 transport 建立独立集成测试套件，每次变更自动验证下游模块

---

### R4: Blocked 项级联阻塞 🟡

**现状**: 26 个 Blocked 项分布在 5 个模块，部分形成阻塞链。

**数据佐证**:

| 模块 | Blocked 数 | 优先级 | 典型阻塞原因 |
|------|-----------|--------|-------------|
| base | 多项 | P1 🟡 | pool_memory_resource 可行性未评估 |
| common | 多项 | P1/P3 | 编译兼容性问题 |
| tools | 多项 | P3 | 文档链接失效、API 文档不匹配 |
| io | 多项 | P1 🟡 | NVTX v2 与 PipelineParallel 编译冲突 |
| transport | 多项 | 隐含 | 等待 memory_pool EPIC |

**级联风险**: base Blocked → transport 延期 → data 延期 → scheduler 延期 → 整个 Phase 0 延期。

**缓解建议**:
1. 立即对 base 模块 `pool_memory_resource` 做技术 Spike（1-2 天），产出 Go/No-Go 决策
2. io 模块 NVTX 编译冲突升级为 P0 处理
3. 每日 Standup 中增加 Blocked 项专项检查

---

### R5: Needs Triage 需求黑洞 🟡

**现状**: 55 个 PRD 处于 Needs Triage 状态，无人认领。

**分布**:
- transport: 21 项（最多，且包含 EPIC 级需求）
- tools: 20 项
- common: 6 项
- base: 5 项

**影响分析**:
- transport 有 21 个未分类项，可能包含与当前 3 个 P0 EPIC 冲突或重复的需求
- 部分 Needs Triage 是 EPIC 级别（如 TMA Exposure、异构范围支持、Docs Overhaul），可能影响路线图规划

**缓解建议**:
1. Manager 实例（当前仅 11 项，其中 10 项 Todo）应将 Triage 作为第一优先级
2. 所有 transport 模块的 Needs Triage 项必须在 Phase 0 结束前完成分类
3. 设定规则：超过 2 周未 Triage 的项自动标记为 P3 并分配到 Phase 1: Skeleton

---

### R6: Skeleton 阶段实现估算不足 🟡

**现状**: 955 个 PRD 标记为 "Phase 1: Skeleton"，全部 Todo，无任何实现进展。

**数据佐证**:
- 955 项占总量的 58.7%
- 主要集中在 common (~300+)、tools (~250+)、io (~120+)、base (~100+)
- 无一项有实现进度估算或工时标注

**影响分析**:
- 当 Phase 0 的 13 个 EPIC 完成后，团队将面对一个 955 项的 "实现悬崖"
- 缺乏分批策略可能导致 Phase 2 变成一个无限期的 Sprint

**缓解建议**:
1. 对 955 项按模块和依赖关系分为 3-4 个子批次
2. 每批次设定独立里程碑和退出标准
3. 优先实现被 P0/P1 项依赖的 Skeleton 项

---

### R7: 跨平台兼容性 🟡

**现状**: 系统需支持 x86 服务器、ARM RK3568、RT-Thread 三类平台。

**风险点**:
- mainboard 模块仅 3 个 PRD，相对于三平台适配的复杂度严重不足
- 部分 base 模块特性（Hopper Cluster、SM120 调优）仅适用于特定 GPU 架构
- RT-Thread 作为 RTOS 与 Linux 环境的资源模型差异巨大

**缓解建议**:
1. 尽早建立三平台 CI 矩阵
2. 明确 RT-Thread 平台的功能子集（不要求全特性支持）
3. mainboard 模块 PRD 需补充平台检测、降级策略、错误恢复的详细需求

---

### R8: API 不稳定导致返工 🟡

**现状**: world::std 和 pub/sub API 仍在活跃开发中，14 个 common EPIC 在 Phase 1 将大幅改变 API surface。

**风险点**:
- Phase 0 开发的上层模块代码可能因 Phase 1 API 变更而需要大规模返工
- 361 个 P2 FEA 中大量依赖 common 模块 API，API 变更的连锁影响巨大

**缓解建议**:
1. Phase 0 结束时冻结 Layer 0-1 的公开 API（base、transport 接口）
2. Phase 1 结束时冻结 Layer 2 API（data、common、scheduler 接口）
3. 上层模块开发基于接口而非实现，降低返工成本

---

### R9: needle-tools 集成风险 🟢

**现状**: 展示层集成尚未启动，当前无相关 PRD。

**风险点**:
- Web 端 GLB/3D 渲染对状态更新的实时性有严格要求
- 1626 个 PRD 中未见 needle-tools 相关需求，集成方案可能缺失

**缓解建议**:
1. Phase 2 期间启动 needle-tools 集成原型
2. 定义 "从个体状态发布到 Web 端渲染帧" 的端到端延迟 SLA

---

### R10: 文档债务 🟢

**现状**: tools 模块有多项文档相关 BUG 和 FEA（链接失效、API 文档不匹配、Contributing 指南过时）。

**缓解建议**:
1. Phase 1 的 "Docs Overhaul" 和 "diataxis" EPIC 可覆盖此风险
2. 建立文档与代码的自动同步检查（CI 中校验文档链接和 API 签名）

---

## 风险热力图

```
        低概率         中概率         高概率        已发生
      ┌────────────┬────────────┬────────────┬────────────┐
高影响 │            │  R3 枢纽   │            │ R1 BUG积压 │
      │            │  R8 API    │            │ R2 过载    │
      ├────────────┼────────────┼────────────┼────────────┤
中影响 │            │  R7 跨平台 │ R5 Triage  │ R4 Blocked │
      │            │  R9 集成   │ R6 Skeleton│            │
      ├────────────┼────────────┼────────────┼────────────┤
低影响 │            │            │            │ R10 文档   │
      └────────────┴────────────┴────────────┴────────────┘
```

---

## 综合建议

**立即行动（本周内）**:
1. 对 138 个 P1 BUG 做二次分级，将编译阻塞类提至 P0
2. 启动 base 模块 pool_memory_resource 技术 Spike
3. Manager 完成 55 个 Needs Triage 项的分类

**短期行动（Phase 0 结束前）**:
1. 重新均衡 Claude 实例工作负载
2. 冻结 transport 抽象层接口
3. 清零 26 个 Blocked 项

**中期行动（Phase 1 期间）**:
1. 将 955 个 Skeleton 项分批规划
2. 建立三平台 CI 矩阵
3. 启动 needle-tools 集成原型
