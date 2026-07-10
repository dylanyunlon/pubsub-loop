# pub/sub-loop 世界模型产品需求文档体系

## 项目定位

pub/sub-loop 是一个**类区块链的世界模型个体系统**，基于 Cyber RT 架构构建。核心理念：

- **个体（Individual）** 是世界的基本单元，每个个体持续发布位置、速度、属性等状态数据
- **pub/sub 循环** 是个体间通信的核心机制，通过 DDS/RTPS 协议实现实时状态同步
- **世界一致性** 类似区块链的状态共识——所有参与者看到一致的世界视图

## 展示层：needle-tools

个体的动态输出通过 [needle-tools](https://github.com/needle-tools)（Needle Engine）在 web 上实时渲染：

- 支持 GLB/glTF 3D 内容的 web 端展示
- 个体位置/属性变化 → GLB 场景中的实时动画
- 支持 Blender 作为个体模型的创作工具
- 基于 three.js 的跨平台渲染（浏览器 → VR 头显）

## 需求文档结构

GitHub Project #4 包含 **1626 个产品需求条目**，覆盖以下维度：

### 模块分布

| 模块 | 条目数 | 命名空间 | 职责 |
|------|--------|----------|------|
| common | 494 | world::std | 标准库算法原语（排序、归约、扫描） |
| tools | 374 | world::tools | 工具链、CI/CD、代码生成 |
| io | 179 | world::io | 高吞吐状态数据 I/O |
| base | 173 | world::base | 内存对齐、原子操作、线程池 |
| data | 90 | world::data | 通道缓冲区、变换归约、多源融合 |
| scheduler | 61 | world::scheduler | croutine 调度、tick-loop |
| component | 41 | world::component | 个体能力、平台适配 |
| profiler | 39 | world::profiler | 性能分析、基准测试 |
| transport | 30 | world::transport | RTPS/共享内存传输 |
| context | 26 | world::context | 执行上下文、Cyber RT 兼容 |
| time | 26 | world::time | 时间系统 |
| message | 22 | world::message | 状态类型、序列化 |
| croutine | 14 | world::croutine | 协程、同步屏障 |
| node | 12 | world::node | 节点驱动、生命周期 |
| 其他 | 33 | - | statistics, record, mainboard, logger 等 |

### 需求类型

| 类型 | 数量 | 说明 |
|------|------|------|
| EPIC | 54 | 史诗级架构需求 |
| FEA | 457 | 功能需求 |
| BUG | 154 | 缺陷修复 |
| THEME | 9 | 主题设计 |
| Other | 941 | 无前缀标签 |

### 优先级

| 级别 | 数量 | 定义 |
|------|------|------|
| P0 🔴 | 53 | 核心架构，阻塞其他工作 |
| P1 🟡 | 154 | 重要功能，当前迭代目标 |
| P2 🟢 | 379 | 次要功能，下一迭代 |
| P3 | 1029 | 基础骨架，长期规划 |

### 开发阶段

| 阶段 | 数量 | 说明 |
|------|------|------|
| Phase 1: Skeleton | 955 | 骨架搭建 |
| Current | 456 | 当前迭代 |
| Next | 172 | 下一迭代 |
| Phase 2: Implementation | 32 | 实现阶段 |

## Claude 工作分配

| Agent | 条目数 | 主要模块 |
|-------|--------|----------|
| Claude-E | 551 | component, tools, context, profiler |
| Claude-D | 546 | transport, data, scheduler |
| Claude-A | 293 | transport, base, data |
| Claude-G | 106 | io, data |
| Claude-B | 81 | common, context |
| Claude-C | 38 | io, node |

## 核心架构 EPIC

1. **统一传输后端** — 将 RTPS + 共享内存合并为单一个体间状态递送管道
2. **多源状态融合引擎** — 并发发布者更新合并为一致的订阅者世界视图
3. **零拷贝异步传输** — 消除个体间状态传输的中间缓冲拷贝
4. **大规模世界缓冲** — 100K+ 个体的溢出背压与分块传递
5. **跨平台初始化** — x86/ARM/RT-Thread 三平台统一引导
6. **平台适配能力集** — 按目标平台分级暴露个体能力
7. **ChannelWriter 优化** — 工作窃取 + Ampere/Hopper 架构感知批量分发
8. **内存池路线图** — 类型化分配器、共享内存生命周期、资源管理

## 个体输出动态 → needle-tools 映射

| 世界模型概念 | needle-tools 渲染 |
|-------------|-------------------|
| 个体位置/速度更新 | GLB 场景中 3D 对象的实时运动 |
| 个体属性变化 | 材质/颜色/大小的动态变化 |
| 个体创建/销毁 | 3D 对象的动态添加/移除 |
| 多源状态融合 | 多视角/多传感器数据的合成视图 |
| 世界状态快照 | 场景截图/离线渲染 |
| tick-loop 节奏 | 动画帧率 |
