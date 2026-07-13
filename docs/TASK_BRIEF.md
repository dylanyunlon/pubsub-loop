# pub/sub-loop Project PRD 生成任务

## 背景
- 项目: github.com/users/dylanyunlon/projects/4
- 项目名: pub/sub-loop
- 性质: 类区块链的世界模型个体系统, Cyber RT架构, DDS/RTPS个体交互
- 展示层: needle-tools (仅展示模块, 不是全部)
- GitHub Token: [REDACTED - provide via environment variable]

## 核心要求
1. 从 Project #4 获取全部现有PRD (Draft Issues)
2. 生成新的高质量PRD, 质量对标 NVIDIA/cccl 级别
3. 完成后直接push到仓库
4. 不开新分支, 不加 v2/v3/port 等后缀
5. 上传到project界面

## PRD质量标准 (参考NVIDIA/cccl风格)
- 明确的依赖关系 (Depends on #xxx)
- 详细的API surface inventory (before/after)
- 具体的代码示例 (当前实现 vs 提议实现)
- 清晰的scope和sub-tasks
- 可量化的acceptance criteria
- Activity记录

## 项目三层架构
- Layer 1 (世界模型层): transport, node, mainboard, record, message, context, logger
- Layer 2 (内部决策层): profiler, statistics, plugin_manager, blocker, scheduler, croutine, service_discovery
- Layer 3 (展示层): needle-tools (GLB/3D web展示)

## Cookie信息 (GitHub session)
见原始消息中的cookie字段

## 注意事项
- 这是产品需求文档(PRD), 不是issue
- pub/sub-loop中的个体不断更新数据和位置
- needle-tools只负责展示模块, 不是所有模块都属于展示
- 目前有30条PRD全部In Progress状态
