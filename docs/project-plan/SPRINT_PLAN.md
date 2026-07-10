# pub/sub-loop Sprint 执行计划

> 生成时间：2026-07-10 | 建议节奏：2 周/Sprint

---

## Sprint 节奏与容量模型

| 参数 | 值 |
|------|-----|
| Sprint 周期 | 2 周 |
| 活跃 Agent | 6（Claude-A/B/C/D/E/G） |
| 每 Agent 每 Sprint 处理能力 | ~15-20 项（含 review） |
| 每 Sprint 总容量 | ~100 项 |
| BUG 修复预留 | 20%（~20 项/Sprint） |

---

## Sprint 0：紧急清理（Week 1-2）

**目标**：清除所有阻塞项和分诊债务，为正式迭代扫清障碍。

### 工作包

| 任务 | 数量 | 负责人 | 完成标准 |
|------|------|--------|---------|
| transport 模块 Needs Triage 分诊 | 21 | Manager + Claude-D | 全部标注优先级和 Sprint |
| 其他模块 Needs Triage 分诊 | 34 | Manager | 全部标注优先级和 Sprint |
| Blocked 项处理——可内部解决 | ~15 | Claude-D/E | 状态变为 In Progress |
| Blocked 项处理——等待外部 | ~11 | Manager | 标注 on-hold + 替代方案 |
| task 模块接口设计 PRD 补充 | +10 | Claude-A | 新增 PRD 入库 |
| event 模块接口设计 PRD 补充 | +4 | Claude-A | 新增 PRD 入库 |

### 退出标准

- Needs Triage = 0
- Blocked（可内部解决）= 0
- task 模块 PRD ≥ 10
- event 模块 PRD ≥ 5

---

## Sprint 1：传输统一（Week 3-4）

**目标**：完成统一传输抽象层，打通 RTPS + 共享内存双路径。

### P0 EPIC 推进

| EPIC | 目标状态 | Agent |
|------|---------|-------|
| 统一 RTPS + 共享内存传输后端 | 抽象层代码完成、UT 通过 | Claude-D |
| transport/memory_pool 路线图 | 类型化分配器接口定义完成 | Claude-A |
| 多源个体状态融合引擎 | 融合策略接口定义 + LastWriteWins 实现 | Claude-A |

### FEA & BUG

| 模块 | FEA | BUG | Agent |
|------|-----|-----|-------|
| transport | 5 | 0 | Claude-D |
| data | 3 | 0 | Claude-A |
| message | 3 | 2 | Claude-E |
| base | 5 | 2 | Claude-E |
| scheduler | 2 | 0 | Claude-D |
| common | 0 | 5 | Claude-B |
| tools | 0 | 3 | Claude-C |

### 里程碑 M1

- `transport::IndividualChannel<T>` 统一抽象可编译
- 融合引擎 LastWriteWins 策略通过单元测试
- P1 BUG 修复 ≥ 12

---

## Sprint 2：零拷贝 + 调度器（Week 5-6）

**目标**：零拷贝传输端到端打通，调度器 warp-scan 产品化。

### P0 EPIC 推进

| EPIC | 目标状态 | Agent |
|------|---------|-------|
| 零拷贝异步个体状态传输协议 | 端到端 benchmark 完成 | Claude-A |
| WSPRO warp-scan 算法产品化 | API 稳定、集成到 scheduler | Claude-D |
| 跨平台世界初始化容器 | x86 路径完成、ARM 路径 50% | Claude-E |

### FEA & BUG

| 模块 | FEA | BUG | Agent |
|------|-----|-----|-------|
| transport | 4 | 0 | Claude-A |
| scheduler | 4 | 0 | Claude-D |
| croutine | 3 | 0 | Claude-D |
| mainboard | 1 | 0 | Claude-E |
| base | 4 | 3 | Claude-E |
| common | 0 | 5 | Claude-B |
| io | 3 | 2 | Claude-G |

### 里程碑 M2

- 零拷贝路径 benchmark：2KB IndividualState 传输延迟 < 10μs
- warp-scan 集成到 scheduler::CRoutine
- P1 BUG 累计修复 ≥ 30

---

## Sprint 3：融合引擎 + 世界查询（Week 7-8）

**目标**：融合引擎完整实现，世界查询算法覆盖核心场景。

### P0 EPIC 推进

| EPIC | 目标状态 | Agent |
|------|---------|-------|
| 多源个体状态融合引擎 | 三种策略全部实现 + 集成测试 | Claude-A |
| data::ChannelWriter 优化 | 工作窃取 + Ampere 感知 | Claude-D |
| 世界查询算法 | traversal/aggregation/set-ops 完成 | Claude-A |
| 大规模世界缓冲 | 背压策略 + 4096 分块可用 | Claude-D |
| top-k 优先选择框架 | rank/filter API 完成 | Claude-G |

### FEA & BUG

| 模块 | FEA | BUG | Agent |
|------|-----|-----|-------|
| data | 8 | 0 | Claude-A/D |
| component | 4 | 0 | Claude-E |
| context | 3 | 0 | Claude-B |
| common | 5 | 5 | Claude-B/G |
| tools | 3 | 3 | Claude-C |
| io | 4 | 2 | Claude-G |

### 里程碑 M3

- 融合引擎 3 策略（LastWriteWins, WeightedAvg, SemanticMerge）通过集成测试
- 10K 个体压力测试通过（mock 传输层）
- Current Sprint In Progress 项 < 50

---

## Sprint 4-6：算法库扩展 + 工具链强化（Week 9-14）

**目标**：common 模块 P0 EPIC 批量推进，CI/文档基础设施搭建。

### Sprint 4 重点

| 主题 | 目标 | Agent |
|------|------|-------|
| std 等价并行算法迁移 | 完成 sort, reduce, scan 三大类 | Claude-D |
| Component 平台适配 | ARM RK3568 能力集定义 | Claude-E |
| CI 第三方测试 | 框架搭建 + 首批 5 个测试 | Claude-C |
| Extended FP 支持 | bf16/fp8 base 层实现 | Claude-E |

### Sprint 5 重点

| 主题 | 目标 | Agent |
|------|------|-------|
| TensorParallel 大输入支持 | > 2^31 元素分块处理 | Claude-D |
| 工作窃取全面铺开 | PipelineParallel 所有算法适配 | Claude-B |
| CI 基准测试 MVP | 自动回归检测 + 看板 | Claude-C |
| io Python 暴露 | neuron::ptx Python binding 完成 | Claude-G |

### Sprint 6 重点

| 主题 | 目标 | Agent |
|------|------|-------|
| 确定性算法 | 全量 reduce/scan 确定性路径 | Claude-D |
| 设备级协作算法 | device-scope cooperative 完成 | Claude-B |
| Diataxis 文档框架 | 迁移完成 50% 文档 | Claude-C |
| Phase 2 内容定义 | 从 Phase 1 毕业项中拆分实现任务 | Manager |

### 里程碑 M4-M6

- M4：common 模块 P0 In Progress ≥ 10
- M5：CI 基准测试看板上线
- M6：Phase 1: Skeleton 完成率 ≥ 40%

---

## Sprint 7-9：骨架批量完成 + L3 启动（Week 15-20）

**目标**：Phase 1 骨架大规模收尾，L3 展示层开工。

### 工作分配

| Sprint | common | io | base | tools | L3 |
|--------|--------|----|------|-------|----|
| S7 | 60 骨架 | 30 骨架 | 20 骨架 | 10 | L3-001 设计 |
| S8 | 60 骨架 | 30 骨架 | 20 骨架 | 10 | L3-001 实现 |
| S9 | 60 骨架 | 30 骨架 | 15 骨架 | 10 | L3-002 实现 |

### 里程碑 M7-M9

- M7：Phase 1: Skeleton 完成率 ≥ 60%
- M8：State Bridge (L3-001) 可发送 DeltaFrame
- M9：Phase 1: Skeleton 完成率 ≥ 80%

---

## Sprint 10-12：收尾 + 集成（Week 21-26）

**目标**：Phase 1 完成，端到端集成验证。

### 关键交付

| 交付物 | Sprint | 验收标准 |
|--------|--------|---------|
| Phase 1 Skeleton 100% 完成 | S10 | 所有 955 项 Done |
| 端到端集成测试套件 | S11 | 个体发布 → 世界快照 → Web 渲染 全链路通过 |
| 100K 个体性能基准 | S11 | 状态同步延迟 < 50ms, GPU 显存 < 4GB |
| needle-tools 渲染演示 | S12 | 浏览器中实时显示 1000 个体的 GLB 场景 |
| Phase 2 完整计划 | S12 | 所有实现阶段 PRD 细化完成 |

---

## Agent 总负载规划

| Agent | S0 | S1 | S2 | S3 | S4-6 | S7-9 | S10-12 | 总计 |
|-------|----|----|----|----|------|------|--------|------|
| Claude-A | 12 | 16 | 18 | 18 | 45 | 30 | 20 | 159 |
| Claude-B | 0 | 8 | 8 | 12 | 40 | 50 | 30 | 148 |
| Claude-C | 0 | 6 | 0 | 8 | 35 | 40 | 30 | 119 |
| Claude-D | 20 | 18 | 18 | 18 | 50 | 60 | 25 | 209 |
| Claude-E | 18 | 18 | 16 | 14 | 50 | 80 | 40 | 236 |
| Claude-G | 0 | 8 | 10 | 14 | 40 | 60 | 30 | 162 |
| Manager | 30 | 6 | 0 | 0 | 10 | 0 | 15 | 61 |

---

## 看板度量（每 Sprint 跟踪）

| 度量 | 计算方式 | S0 目标 | S3 目标 | S12 目标 |
|------|---------|---------|---------|----------|
| 吞吐量 | Done items / Sprint | 80 | 60 | 100 |
| 阻塞率 | Blocked / Total Active | < 5% | < 2% | 0% |
| P0 进度 | P0 Done / P0 Total | 0% | 40% | 100% |
| P1 BUG 存量 | Open P1 BUGs | 138 | 80 | 0 |
| PRD 完整率 | Active items with body > 1000 chars | 20% | 50% | 90% |
| 分诊债务 | Needs Triage count | 0 | 0 | 0 |
