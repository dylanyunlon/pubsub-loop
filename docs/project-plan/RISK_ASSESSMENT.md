# pub/sub-loop 风险评估报告

> 生成时间：2026-07-10 | 数据源：1626 PRDs 全量分析

---

## 一、风险总览

| 风险等级 | 数量 | 说明 |
|---------|------|------|
| 🔴 严重 | 5 | 可能导致项目交付失败 |
| 🟡 高 | 6 | 显著影响进度或质量 |
| 🟢 中 | 4 | 需关注但可控 |

---

## 二、严重风险（🔴）

### R1: transport 模块 70% 条目未分诊

**现象**：transport 共 30 个 PRD，其中 21 个处于 Needs Triage 状态（70%）。transport 是个体间状态传输的唯一路径，承载 3 个 P0 EPIC（统一传输后端、零拷贝传输、内存池路线图），且被 4 个上层模块直接依赖。

**影响**：transport 分诊不完成，无法确认 P0 EPIC 的子任务拆分是否完整，Phase 1A 的里程碑无法锁定。

**缓解措施**：
- 立即由 Manager 主持 transport 模块全量分诊会议
- 将 21 个 Needs Triage 项在 1 个工作日内完成优先级和 Sprint 标注
- 识别哪些是 P0 EPIC 的子任务，哪些可降为 P3

**责任人**：Manager + Claude-D

---

### R2: task 和 event 模块定义严重不足

**现象**：task 模块仅 2 个 PRD，event 模块仅 1 个 PRD，但 task 被 15 个模块依赖（所有模块中最高），event 被 3 个模块依赖。

**影响**：task 作为协程任务抽象，其接口稳定性直接影响 scheduler、data、transport 等核心模块。当前 2 个 PRD 远不足以定义完整的任务生命周期、取消语义、错误传播机制。如果后期发现接口设计缺陷，修改成本将波及 15 个模块。

**缓解措施**：
- 立即补充 task 模块的接口设计 PRD（建议新增 8-12 个 FEA 级 PRD）
- 对 event 模块同样补充定义（建议新增 3-5 个 PRD）
- 在 Phase 1A 开始前锁定 task 和 event 的公开 API

**责任人**：Manager + Claude-A

---

### R3: 完成率极低（0.25%），阻塞项持续累积

**现象**：1626 个 PRD 中仅 4 个 Done（0.25%），26 个 Blocked，55 个 Needs Triage。Current Sprint 的 467 项中，仍有 10 项 Todo 未启动、26 项 Blocked。

**影响**：按当前速度，项目无法在合理时间内交付。Blocked 项如不处理会形成连锁阻塞——当前 blocked 集中在 common（12）、Claude-E（10）、Claude-D（9），这两个 Agent 恰好是负载最重的（各 550+ 项）。

**缓解措施**：
- 设立每日 stand-up：清点 blocked 项，区分"等待外部"和"可内部解决"
- 对"等待外部依赖"的 blocked 项（如依赖 libcu++ 发布），设置明确超时并准备替代方案
- 将 Claude-E 和 Claude-D 的部分任务转移给 Claude-A/G/C

**责任人**：Manager

---

### R4: P0 EPIC 中 36 项仍为 Todo，未启动

**现象**：56 个 P0 项中，36 个状态为 Todo（Next Sprint），仅 13 个 In Progress，1 个 In Review。这些 P0 EPIC 覆盖：common 算法标准化（14 个）、data 管道安全性（3 个）、base 底层能力（4 个）、tools CI/文档（7 个）、io Python 暴露（2 个）等。

**影响**：P0 定义为"核心架构，阻塞其他工作"。36 个 P0 未启动意味着项目核心架构仍有大量空白。特别是 `data pipeline must not invoke user ops on out-of-bounds message buffers` 这种安全性 EPIC 未启动，可能导致后期大规模返工。

**缓解措施**：
- 对 36 个 P0 Todo 逐一评审：是否真正 P0？能否降级？
- 对确认 P0 的项，按依赖关系排入 Sprint 1-3
- 安全类 EPIC 优先于性能优化类 EPIC

**责任人**：Manager + 对应 Claude Agent

---

### R5: PRD 文档完整度低——83.6% 需细化

**现象**：1626 个 PRD 中，1359 个（83.6%）body 长度 < 1000 字符，属于"需细化"级别。仅 89 个（5.5%）达到完整 PRD 标准（> 5000 字符）。

**影响**：不充分的 PRD 导致实现歧义、返工、模块间接口不匹配。尤其是 Phase 1: Skeleton 的 955 项中，大量是粗粒度描述，进入实现阶段时需要大幅补充细节。

**缓解措施**：
- 对 P0/P1 项强制要求完整 PRD（含接口定义、验收标准、依赖声明）
- P2/P3 项可保持骨架状态，进入 Current Sprint 前补充
- 建立 PRD 模板，确保每个 PRD 包含：问题描述、接口签名、验收标准、依赖模块、估时

**责任人**：Manager

---

## 三、高风险（🟡）

### R6: 138 个 P1 BUG 未修复

所有 154 个 BUG 中，138 个为 P1（占 89.6%），0 个已修复。P1 BUG 集中在 common（45）、tools（36）、io（30）。编译错误、算法正确性问题如不修复，会阻塞 FEA 开发。

**缓解**：每个 Sprint 分配 20% 产能专门修 BUG，优先修复阻塞 P0 EPIC 的 BUG。

---

### R7: Claude-E 和 Claude-D 负载严重不均

Claude-E（551 项）和 Claude-D（546 项）合计承担 67.5% 的工作量，而 Claude-C（38 项）和 Claude-B（81 项）相对空闲。负载不均导致瓶颈集中在 E/D，两者的 blocked 项也最多（E:10, D:9）。

**缓解**：重新平衡分配，将 common/tools 的 P3 骨架任务批量转移给 Claude-B/C/G。

---

### R8: transport ↔ data 循环依赖

transport 依赖 data（缓冲管理），data 依赖 transport（底层传输）。这种循环依赖会导致接口变更相互影响，增加集成风险。

**缓解**：定义明确的接口边界层（`transport::BufferPolicy` 抽象），打破编译时循环依赖。

---

### R9: 展示层（L3）PRD 缺失

State Bridge（L3-001）和 DeltaFrame 协议（L3-002）仅有标题级描述，尚未拆分为 FEA/TASK。needle-tools 集成完全没有 PRD。

**缓解**：Phase 1A 末尾开始补充 L3 层 PRD，预估需新增 15-25 个 FEA 级条目。

---

### R10: Phase 2: Implementation 29/32 项 Needs Triage

Phase 2 的 32 个条目中，29 个 Needs Triage，说明实现阶段的工作定义几乎为零。

**缓解**：Phase 1 中期（Sprint 6 左右）开始系统性定义 Phase 2 内容。

---

### R11: 跨平台支持的验证覆盖不足

mainboard 跨平台引导覆盖 x86/ARM RK3568/RT-Thread 三个平台，但 CI 中没有 ARM 和 RT-Thread 的自动化测试。

**缓解**：Sprint 3 开始搭建 QEMU-based ARM CI，RT-Thread 可用模拟器验证。

---

## 四、中等风险（🟢）

### R12: 大规模个体（100K+）的性能验证推迟

100K 个体的背压和分块传递是 P0 EPIC，但性能验证需要完整的传输+数据+调度管道。在 Phase 1 末尾才能开始真实压力测试，发现问题后修改成本高。

**缓解**：Sprint 2 开始用 mock 传输层进行 10K 级预验证，尽早暴露瓶颈。

---

### R13: 文档和 CI 工具链的 P0 EPIC 偏多

tools 模块有 7 个 P0 EPIC，但工具链通常不应是 P0（不阻塞核心功能）。可能存在优先级膨胀。

**缓解**：评审 tools 的 P0 EPIC，考虑将文档类降为 P1，仅保留 CI 测试/sanitizer 为 P0。

---

### R14: 多语言混合（Mixed 占 86.6%）增加构建复杂度

1408 个 PRD 标记为 Mixed 语言，实际涉及 C++/CUDA/Python/LaTeX 混合。构建系统需处理多工具链集成。

**缓解**：tools 模块的 CI 强化 EPIC 需要覆盖多语言构建矩阵。

---

### R15: 无明确的发布计划和版本策略

1626 个 PRD 中没有发现版本号、发布日期、或里程碑截止时间的定义。所有时间线都是相对的（Current/Next/Phase 1/Phase 2），缺乏绝对日期约束。

**缓解**：在 ROADMAP 中补充绝对时间线，建议每 2 周一个 Sprint，Phase 1 目标 24 周。

---

## 五、风险热力图

```
影响 ↑
高    │  R1(transport)   R3(完成率)    R4(P0未启动)
      │  R2(task/event)  R5(PRD质量)
      │
中    │  R6(P1 BUG)      R7(负载不均)  R8(循环依赖)
      │  R9(L3缺失)      R10(Phase2)   R11(跨平台CI)
      │
低    │  R12(100K验证)   R13(P0膨胀)   R14(多语言)
      │  R15(无发布计划)
      └──────────────────────────────────────────→ 概率
          低              中              高
```

---

## 六、Top 5 行动项

| 优先级 | 行动 | 风险 | 截止 |
|--------|------|------|------|
| 1 | transport 模块全量分诊 | R1 | Week 1 |
| 2 | task/event 模块接口补充 | R2 | Week 2 |
| 3 | 26 个 Blocked 项逐一处理 | R3 | Week 1 |
| 4 | P0 Todo 项评审和排期 | R4 | Week 2 |
| 5 | Claude 负载重新平衡 | R7 | Week 1 |
