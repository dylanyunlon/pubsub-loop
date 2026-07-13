# Claude Agent 分步派遣协议

## 核心原则

Claude Code 小弟在新对话中是空白的。必须分步骤建立上下文，不能一上来就给 token。

## 派遣步骤

### Step 1: 喂项目上下文（第一条消息）

发送 `docs/PROJECT_CONTEXT_FOR_AGENTS.md` 的内容，让 Claude 理解：
- pub/sub-loop 是什么项目
- 三层架构（Layer 1/2/3）
- 现有 PRD 的风格和质量标准
- 哪些模块/PRD 需要补充

消息模板：
```
你是 pub/sub-loop 项目的开发者。这是项目的完整上下文：

[粘贴 PROJECT_CONTEXT_FOR_AGENTS.md 内容]

请确认你理解了项目架构和PRD风格。
```

### Step 2: 分配具体任务（第二条消息）

告诉 Claude 具体要做什么，包括：
- 要生成哪些模块的 PRD
- PRD 输出格式（JSON 数组）
- 不要开新分支、不要加 v2/v3 后缀

消息模板：
```
现在你的任务是为以下模块生成高质量 PRD：

[具体模块和PRD标题列表]

输出格式为 JSON 数组，每个元素包含：
- title: PRD 标题
- body: PRD 正文（markdown，≥3000字符）
- module: 模块名
- priority: P0/P1/P2/P3
- sprint: Current/Next/Phase 1: Skeleton
- status: Todo
- claude: Claude-A/Claude-D/Claude-E
- language: C++/CUDA/Python/Mixed

请直接输出 JSON，不要其他解释。
```

### Step 3: 补充认证信息（第三条消息）

在 Claude 已经理解项目并生成了 PRD 内容后，再给认证信息：

```
很好。现在请用以下信息将 PRD 推送到 GitHub Project #4：

GitHub 仓库: dylanyunlon/pubsub-loop
Project ID: PVT_kwHODJPfps4Bc4xI

请通过 GraphQL API 创建 Draft Issues 并设置字段。
完成后直接 push 到 main 分支，不开新分支。

[此处提供 token]
```

### Step 4: 发送 "Continue"

如果输出被截断，发送 `Continue` 让 Claude 继续执行。

## 注意事项

1. **不要在 Step 1/2 中包含任何 token、key、cookie**
2. **不要让 Claude 开新分支或加 v2/v3 后缀**
3. **每个 Claude 实例负责不同模块的 PRD，避免冲突**
4. **PRD 质量对标 NVIDIA/cccl 水平**

## 推荐的 Claude 实例分工

| Claude 实例 | 负责模块 | 重点 |
|------------|---------|------|
| Claude-A | transport, data, base | 核心传输和数据层 |
| Claude-D | scheduler, croutine, message, component | 调度和组件层 |
| Claude-E | tools, mainboard, context, profiler | 工具链和基础设施 |
| Manager | 跨模块 EPIC、Layer 3 集成 | 全局架构 |

## 字段 ID 参考（用于 GraphQL API）

```
Project ID: PVT_kwHODJPfps4Bc4xI

Status field:  PVTSSF_lAHODJPfps4Bc4xIzhXeTvo
  Todo:        f75ad846
  In Progress: 47fc9ee4

Module field:  PVTSSF_lAHODJPfps4Bc4xIzhXeVzQ
  transport:   b28cb884
  data:        0953bf92
  common:      0d9aa4e3
  io:          76f13f54
  base:        906b730e
  component:   172fa262
  scheduler:   a3a86687
  context:     73cd4e08
  profiler:    75f85b2d
  message:     5dcf4f88
  mainboard:   d98dafbd
  tools:       560b7d14
  (完整列表见 scripts/generate_and_push_prds.py)

Priority field: PVTSSF_lAHODJPfps4Bc4xIzhXeV1Q
  P0: e776b235
  P1: ff4d1119
  P2: 012ba1af
  P3: ba3190c6

Sprint field: PVTSSF_lAHODJPfps4Bc4xIzhXeVzU
  Current: 60c8c15c
  Next:    4b59b246

Claude field: PVTSSF_lAHODJPfps4Bc4xIzhXeVzY
  Claude-A: d9b45e12
  Claude-D: 756df240
  Claude-E: 05b56fe6
  Manager:  12586bbe
```
