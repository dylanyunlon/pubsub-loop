# transport 模块需求规格

## 命名空间
`world::transport`

## 模块职责
世界模型传输层——RTPS/共享内存/进程内传输，负责个体间状态数据的实时可靠递送。

## 核心架构

```
┌─────────────────────────────────────────────────┐
│           transport::UnifiedWriter<T>           │
│  ┌──────────┐  ┌──────────┐  ┌───────────────┐ │
│  │ QoS      │  │ History  │  │ Sequence      │ │
│  │ Enforcer │  │ Cache    │  │ Numbering     │ │
│  └────┬─────┘  └────┬─────┘  └──────┬────────┘ │
│       └──────────────┼───────────────┘          │
│              ┌───────▼────────┐                 │
│              │ BackendRouter  │                 │
│              └───┬────────┬───┘                 │
│          ┌───────▼──┐ ┌───▼──────────┐          │
│          │ ShmSink  │ │  RtpsSink    │          │
│          └──────────┘ └──────────────┘          │
└─────────────────────────────────────────────────┘
```

## 个体输出动态关联
transport 是个体状态从发布者到订阅者的物理通路。传输延迟和可靠性直接影响 needle-tools GLB 渲染中个体运动的实时性。

## EPIC 需求（P0）

1. **统一 RTPS + 共享内存后端** — UnifiedWriter/UnifiedReader + BackendRouter
2. **零拷贝异步传输** — ZeroCopyRegion + 惰性序列化
3. **内存池路线图** — TypedPool + SharedSegmentManager + ResourceAccounting

## 与 needle-tools 的数据流

```
Individual → publish(state) → UnifiedWriter → BackendRouter
                                                ↓
                              [ShmSink | RtpsSink | IntraProcess]
                                                ↓
                              UnifiedReader → subscriber callback
                                                ↓
                              needle-tools GLB state update
                                                ↓
                              three.js scene render
```

## 条目统计
- 总数：30
- P0：5 | P1：3 | P2：5 | P3：17
- 全部已有详细自定义描述
