# pub/sub-loop 产品需求构建任务分派

## 背景

pub/sub-loop 项目有 1615 个 Draft Issue 作为产品需求文档。  
需要将这些需求适配为真正符合「世界模型个体系统 + Cyber RT pub/sub + needle-tools 动态输出」主题的产品需求。

## 当前状态

- ✅ **588 个条目** 已有详细的自定义描述（主要在 transport, tools, common 模块的深度设计）
- ✅ **1026 个条目** 已完成第一轮适配：
  - 连接到 world:: 命名空间
  - 关联 pub/sub-loop 世界模型上下文
  - 标注个体输出动态（needle-tools GLB 渲染）关联

## 下一步：深度需求细化

每个 Claude agent 负责其分配的模块，进行第二轮细化：

### Claude-A（293 条目：transport, base, data）
- 重点：零拷贝传输、内存对齐、状态融合的详细技术方案
- 确保 transport 层的接口设计与 needle-tools 的数据消费模式匹配

### Claude-B（81 条目：common, context）
- 重点：命名空间统一（cyber:: → world::）的迁移计划
- 底层 pub/sub API 与高层节点 API 的层次关系

### Claude-C（38 条目：io, node）
- 重点：高吞吐 I/O 通道设计、节点生命周期管理
- 个体 GLB 模型的加载/卸载与节点生命周期的映射

### Claude-D（546 条目：transport, data, scheduler）
- 重点：ChannelWriter 优化、tick-loop 调度、状态缓冲
- 确保调度频率满足 needle-tools 的渲染帧率要求

### Claude-E（551 条目：component, tools, context, profiler）
- 重点：平台适配能力集、工具链、性能分析
- CI/CD 流水线覆盖 needle-tools 集成测试

### Claude-G（106 条目：io, data）
- 重点：批量 TopK 选择、流式编解码
- 大规模世界中个体可见性筛选（决定哪些个体需要渲染）

## 关键约束

- GitHub key: 使用已配置的 token
- 直接在 main 分支操作，不开新分支
- 不使用 v2/v3 等版本后缀
- Project #4 是唯一的需求管理界面

## 多维度评估标准

每个需求条目应满足：

1. **技术完整性** — 有清晰的接口设计、依赖关系、验收标准
2. **世界模型关联** — 明确说明该需求在 pub/sub-loop 世界模型中的角色
3. **个体动态输出** — 说明该需求如何影响最终的 needle-tools GLB 渲染效果
4. **跨模块依赖** — 标注与其他模块的依赖和接口约定
5. **平台覆盖** — 考虑 x86/ARM/嵌入式三平台的适用性
