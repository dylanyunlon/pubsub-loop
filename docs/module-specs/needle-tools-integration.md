# needle-tools 集成需求规格

## 概述

[needle-tools](https://github.com/needle-tools)（Needle Engine）是 pub/sub-loop 世界模型的展示层，负责将个体状态变化渲染为 web 端的 3D 动态效果。

## Needle Engine 能力

- 基于 three.js 的 web 3D 引擎
- 支持 GLB/glTF 模型加载和实时渲染
- 支持从 Blender/Unity 导出场景
- 跨平台：浏览器 → 移动端 → VR 头显
- npm 包：`@needle-tools/engine`

## 个体状态 → GLB 渲染映射

| 个体状态字段 | GLB 渲染属性 | 更新频率 |
|-------------|-------------|---------|
| IndividualPos (x,y,z) | Object3D.position | 30-60 Hz |
| IndividualVelocity | 运动插值/外推 | 30-60 Hz |
| IndividualOrientation (quaternion) | Object3D.quaternion | 30-60 Hz |
| IndividualAttributes (float[]) | Material uniforms / Scale | 10 Hz |
| IndividualMeshRef (asset_id) | GLB model swap | 事件驱动 |
| IndividualLifecycle (alive/dead) | Scene add/remove | 事件驱动 |

## 数据流架构

```
[World Model]                    [Rendering Layer]
                                 
Individual A ──pub──→ ┐          
Individual B ──pub──→ ├→ State   WebSocket/    Needle Engine
Individual C ──pub──→ ┘  Buffer  SSE Stream → (three.js)
                         │                    ↓
                         │              GLB Scene
                         │              ├─ Object A (pos, rot, scale)
                         │              ├─ Object B (pos, rot, scale)  
                         │              └─ Object C (pos, rot, scale)
                         │
                    [Subscriber]
                    Aggregates world state
                    into renderable snapshot
```

## 性能要求

| 指标 | 目标值 | 说明 |
|------|--------|------|
| 状态到渲染延迟 | < 50ms | 从个体 publish 到 GLB 场景更新 |
| 渲染帧率 | ≥ 30 FPS | needle-tools 场景渲染 |
| 同时渲染个体数 | ≥ 1000 | 浏览器端 |
| GLB 模型切换 | < 200ms | 个体类型变更时 |
| 初始加载 | < 3s | 首次场景加载 |

## 与 pub/sub-loop 模块的依赖关系

| 模块 | 关联 | 说明 |
|------|------|------|
| transport | 直接 | 状态数据的传输通路 |
| data | 直接 | 状态聚合和融合后的世界视图 |
| scheduler | 间接 | tick-loop 频率决定更新帧率 |
| message | 直接 | 状态序列化格式（WebSocket 传输） |
| node | 间接 | 个体生命周期 → 3D 对象生命周期 |
| record | 可选 | 世界快照 → 离线渲染/回放 |

## Blender 工作流

```
Blender 建模 → 导出 GLB → Needle Engine 加载
                ↑
                │
    个体类型定义（mesh, material, animation）
    每种个体类型对应一个 GLB 模型模板
    运行时通过 IndividualMeshRef 引用
```

## 待定设计决策

1. **状态传输协议**：WebSocket vs Server-Sent Events vs WebRTC DataChannel
2. **状态压缩**：是否对高频位置更新做 delta 编码
3. **LOD 策略**：远处个体降低更新频率 / 简化模型
4. **插值策略**：客户端预测 vs 服务端权威
