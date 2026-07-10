# pub/sub-loop 模块依赖图

> 基于 1626 个 PRD 正文中的跨模块引用分析 · 生成日期: 2026-07-10

---

## 一、系统分层架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                        展示层 (needle-tools / Web)                   │
│                        GLB/3D 渲染 · 个体可视化                       │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐    │
│  │  node    │  │ context  │  │ profiler │  │   statistics    │    │
│  │ (12 PRD) │  │ (26 PRD) │  │ (39 PRD) │  │    (4 PRD)     │    │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └───────┬────────┘    │
│       │              │              │                │              │
│  ┌────▼──────────────▼──────────────▼────────────────▼────────┐    │
│  │                    component (41 PRD)                       │    │
│  │         个体能力集 · DynamicComponentBase · 插件管理          │    │
│  └──────────────────────────┬────────────────────────────────┘    │
│                              │                                     │
│  ┌───────────────────────────▼────────────────────────────────┐    │
│  │                    common (494 PRD)                         │    │
│  │    world::std 并行算法 · TensorParallel · PipelineParallel   │    │
│  └──────┬──────────────┬───────────────────┬─────────────────┘    │
│         │              │                   │                       │
│  ┌──────▼─────┐  ┌─────▼──────┐  ┌────────▼──────┐               │
│  │  data      │  │ scheduler  │  │    io          │               │
│  │ (90 PRD)   │  │ (61 PRD)   │  │  (179 PRD)    │               │
│  │ 状态融合    │  │ CRoutine   │  │  序列化/流     │               │
│  │ 世界查询    │  │ 工作窃取    │  │  PTX 绑定     │               │
│  └──────┬─────┘  └─────┬──────┘  └───────────────┘               │
│         │              │                                           │
│  ┌──────▼──────────────▼──────────────────────────────────────┐    │
│  │                  transport (30 PRD)                          │    │
│  │     RTPS · 共享内存 · memory_pool · 零拷贝传输                │    │
│  └──────────────────────────┬────────────────────────────────┘    │
│                              │                                     │
│  ┌───────────────────────────▼────────────────────────────────┐    │
│  │                     base (173 PRD)                          │    │
│  │     原子操作 · 浮点类型 · 内存分配器 · mdspan · Hopper 支持    │    │
│  └────────────────────────────────────────────────────────────┘    │
│                                                                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐    │
│  │mainboard │  │  tools   │  │  time    │  │    croutine     │    │
│  │ (3 PRD)  │  │(374 PRD) │  │ (26 PRD) │  │   (14 PRD)     │    │
│  │ 世界引导  │  │ CI/文档  │  │ 异步模型  │  │   协程原语      │    │
│  └──────────┘  └──────────┘  └──────────┘  └──────────────────┘    │
├─────────────────────────────────────────────────────────────────────┤
│  辅助模块: message(22) · logger(3) · parameter(4) · record(4)      │
│            service(3) · service_discovery(2) · blocker(2)           │
│            task(2) · event(1) · sysmo(4) · plugin_manager(2)       │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 二、跨模块引用矩阵

基于 PRD 正文中 `module::` 命名空间引用和显式依赖声明提取：

```
                base  transport  data  scheduler  message  component  croutine  node  profiler  time
base              ·                                                                              
transport         ✦✦       ·      ✦✦✦✦   ✦✦✦      ✦✦✦      ✦✦                  ✦      ✦✦✦         
data              ✦                ·      ✦                                                      
scheduler                                 ·                            ✦                          
common           ✦✦✦     ✦✦       ✦✦✦    ✦✦✦                ✦         ✦                    ✦✦✦   
profiler                 ✦✦       ✦       ✦                            ✦                          
node                              ✦                                    ·                          
statistics               ✦                                                                       
```

✦ = 1-2 次引用 | ✦✦ = 3-4 次 | ✦✦✦ = 5-6 次 | ✦✦✦✦ = 7+ 次

**最密集依赖路径**: transport → data (7次), transport → scheduler (6次), common → time (6次), common → scheduler (6次)

---

## 三、EPIC 级依赖链

### 关键路径 1: 传输统一链

```
[EPIC] transport/memory_pool roadmap (P0, transport)
    │
    ├──► [EPIC] Unify RTPS + SHM backends (P0, transport)
    │        │
    │        └──► [EPIC] Zero-copy async transfer (P0, transport)
    │                 │
    │                 └──► [EPIC] Large-population buffering 100K+ (P0, data)
    │                          │
    │                          └──► [EPIC] Top-k priority selection (P0, data)
    │
    └──► [FEA] Pluggable memory allocator (P2, base)
              depends on base atomics
```

### 关键路径 2: 调度融合链

```
[EPIC] WSPRO warp-scan scheduler (P0, scheduler)
    │
    ├──► [EPIC] ChannelWriter pub/sub dispatch (P0, data)
    │        │
    │        └──► [EPIC] Multi-source state fusion engine (P0, data)
    │
    └──► [EPIC] PipelineParallel Performance Tuning (P0, scheduler)
              │
              └──► [EPIC] Use work stealing in all PP algorithms (P0, common)
```

### 关键路径 3: 计算基础链

```
[EPIC] Extended Floating-Point (P0, base)
    │
    ├──► [EPIC] Hopper Cluster support (P0, base)
    │        │
    │        └──► [EPIC] Tune PP Device primitives for SM120 (P0, common)
    │
    ├──► [EPIC] Atomics Improvements (P0, base)
    │        │
    │        └──► [EPIC] mdspan-based algorithms (P0, base)
    │
    └──► [EPIC] Fork/Join Parallel Ranges (P0, base)
              │
              └──► [EPIC] Port std parallel algorithms → world::std (P0, common)
```

### 关键路径 4: 平台适配链

```
[EPIC] Setup cross-platform world init container (P0, mainboard)
    │
    ├──► [EPIC] Component 平台分级暴露 (P0, component)
    │        │
    │        └──► [EPIC] 并发哈希表 → DynamicComponentBase (P0, component)
    │
    └──► [EPIC] Neuron Next async programming model (P0, time)
```

---

## 四、模块耦合度评估

| 模块 | 被引用次数 | 引用他模块次数 | 耦合评级 | 风险说明 |
|------|-----------|--------------|---------|---------|
| transport | 高 (被 data/profiler/statistics 引用) | 极高 (引用 7 个模块) | 🔴 枢纽 | 传输层变更影响全链路 |
| data | 高 (被 transport/common/node/profiler/tools 引用) | 中 | 🔴 枢纽 | 状态模型是全系统核心 |
| common | 中 | 极高 (引用 7 个模块) | 🟡 扇出 | 算法库依赖广泛但单向 |
| scheduler | 高 (被 transport/common/data/profiler 引用) | 低 | 🟡 被依赖 | 调度策略变更波及上层 |
| base | 高 (被 transport/data/common 引用) | 无 | 🟢 叶子 | 稳定底层，变更可控 |
| message | 中 (被 transport 引用 5 次) | 无 | 🟢 叶子 | 消息格式变更需谨慎 |

---

## 五、Blocked 项依赖分析

当前 26 个 Blocked 项的模块分布和阻塞原因：

| 模块 | Blocked 数 | 典型阻塞原因 |
|------|-----------|-------------|
| transport | 多项 | 等待 memory_pool 和统一后端 EPIC |
| common | 多项 | 等待 base 模块 atomics/FP 改进 |
| base | 多项 | 等待 pool_memory_resource 可行性评估 |
| tools | 多项 | 等待上游 API 稳定 |
| io | 多项 | 等待 NVTX/PipelineParallel 编译冲突解决 |

**解除阻塞的推荐顺序**:
1. base 模块 `pool_memory_resource` 评估 → 解除 base 阻塞
2. transport `memory_pool` EPIC 推进 → 解除 transport 阻塞
3. common 编译错误修复 (string + tuple) → 解除 common 阻塞
4. io NVTX 冲突解决 → 解除 io 阻塞

---

## 六、建议的构建顺序

基于依赖拓扑排序的最优构建序列：

```
Layer 0 (无外部依赖):  base · time · croutine · message · logger
         │
Layer 1 (依赖 Layer 0): transport · scheduler
         │
Layer 2 (依赖 Layer 1): data · io · common
         │
Layer 3 (依赖 Layer 2): component · context · profiler
         │
Layer 4 (依赖 Layer 3): node · mainboard
         │
Layer 5 (依赖 Layer 4): tools · statistics · sysmo
         │
Layer 6 (集成层):       needle-tools Web 渲染
```

同一 Layer 内的模块可以并行开发，跨 Layer 必须按序。
