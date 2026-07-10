# pub/sub-loop 产品需求构建任务分派

## 背景

pub/sub-loop 项目有 1626 个 Draft Issue 作为产品需求文档。
需要将这些需求提升到 NVIDIA/CCCL 级别的质量标准。

## 当前状态（2026-07-10 更新）

- 全部 1626 个条目已拉取并结构化
- 按模块、sprint、优先级多维度拆分完成
- 分为 5 个批次供多个 Claude agent 并行处理

### 数据统计

| 指标 | 数量 |
|------|------|
| 总条目 | 1626 |
| 已有详细描述（>1000字符）| 267 |
| 需要细化（<1000字符）| 1359 |
| P0 关键路径 | 56 |
| Blocked | 26 |
| Done | 4 |

## 批次分配

### Batch 1: common + tools（868 items）
- 模块：common (494), tools (374)
- 需要细化：680 items
- 数据：`data/batches/batch1_common_tools.json`
- 重点：world::std 算法原语、CI/CD 工具链
- 内部决策系统的基础算法层

### Batch 2: 核心模块（195 items）
- 模块：transport (30), data (90), scheduler (61), croutine (14)
- 需要细化：147 items
- 数据：`data/batches/batch2_core.json`
- 重点：零拷贝传输、状态融合、tick-loop 调度
- 这是世界模型的心脏——个体间通信和状态同步

### Batch 3: 基础设施（352 items）
- 模块：base (173), io (179)
- 需要细化：344 items
- 数据：`data/batches/batch3_infra.json`
- 重点：内存对齐、原子操作、高吞吐 I/O
- 支撑上层所有模块的底层能力

### Batch 4: 上层模块（127 items）
- 模块：component (41), context (26), time (26), message (22), node (12)
- 需要细化：118 items
- 数据：`data/batches/batch4_upper.json`
- 重点：平台适配、执行上下文、消息类型
- 个体的能力定义和生命周期管理

### Batch 5: 辅助模块（84 items）
- 模块：profiler (39) + statistics, record, sysmo, parameter 等小模块
- 需要细化：70 items
- 数据：`data/batches/batch5_aux.json`
- 重点：性能分析、监控、诊断
- 保障世界模型的可观测性

## 每个 agent 的指令

1. 读取 `docs/project-plan/DISPATCH_INSTRUCTIONS.txt` 了解项目背景
2. 读取对应 batch 的 JSON 数据文件
3. 对 `body_len < 1000` 的 item，生成 CCCL 级别的产品需求描述
4. 通过 GraphQL API 更新 Project #4 中对应 item 的 body
5. 更新 `docs/module-specs/` 下的模块规格文档
6. 直接 push 到 main 分支

### GraphQL 更新示例

```graphql
mutation {
  updateProjectV2DraftIssue(input: {
    projectId: "PVT_kwHODJPfps4Bc4xI"
    draftIssueId: "<item.item_id>"
    body: "<新的需求描述>"
  }) {
    draftIssue { id title body }
  }
}
```

### Token

```
使用环境变量 GITHUB_TOKEN
```

## 约束

- 直接在 main 分支操作
- 不开新分支、不加 v2/v3 后缀
- Project #4 是唯一的需求管理界面
- 不是所有模块都牵扯 web 3D，很多是后台系统
- needle-tools 的 web 3D 展示是额外的最终产品输出
- 内部决策系统消费是核心
