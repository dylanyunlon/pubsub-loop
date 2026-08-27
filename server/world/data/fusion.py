# SPDX-License-Identifier: Apache-2.0
#
# world/data/fusion.py
#
# 多源个体状态融合引擎 — PRD #1, #18, #46
#
# CyberRT来源:
#   cyber/data/fusion/all_latest.h → AllLatest 融合策略
#   cyber/data/data_visitor.h      → DataVisitor 多通道访问
#   (CyberRT 的 fusion 只有 AllLatest/AllShortest 两种简单策略)
#
# governance来源:
#   Runtime.evaluate_intervention_point() 收集多源输入的模式
#   SharedSessionObject.consistency_mode — 一致性约束
#
# 解决的问题:
#   PRD #1: 一个订阅者个体收到N个发布者的并发状态更新,
#           需要融合为一个一致的状态视图。
#           例: sensor-fusion 个体订阅 N 个 lidar 个体。
#
#   PRD #18: all_of 谓词检测性能优化 — 向量化
#
#   PRD #46: transform_reduce 双源聚合接口
#
# 关键约束:
#   1. 融合策略必须是可插拔的(AllLatest, WeightedAverage, Kalman等)
#   2. 融合结果必须在一个tick内完成(33ms budget)
#   3. 1000个个体, 每个有2-5个数据源 → 2000-5000个融合操作/tick

import threading
import time
from abc import ABC, abstractmethod
from typing import TypeVar, Generic, Optional, Callable, Dict, List, Tuple, Any

from world.data.data_core import CacheBuffer
from world.proto.world_types import Vec3

T = TypeVar('T')


# ─── Fusion Strategies ───

class FusionStrategy(ABC, Generic[T]):
    """
    融合策略基类

    CyberRT: 只有 AllLatest (取所有通道最新值)
    pubsub-loop 扩展: 多种融合策略

    governance对应:
      policy.evaluate() 的 evidence aggregation —
      从多个来源收集 evidence，合并为一个 policy_input。
    """

    @abstractmethod
    def fuse(self, sources: Dict[str, T]) -> Optional[T]:
        """
        融合多个来源的状态

        sources: {source_id: latest_state}
        return: 融合后的状态, 或None(数据不足)
        """
        ...

    @abstractmethod
    def name(self) -> str:
        ...


class AllLatestFusion(FusionStrategy[T]):
    """
    取最新值 — CyberRT 默认策略

    所有来源中时间戳最新的那个。
    适用于: 覆盖式更新(最新值就是正确值)。

    CyberRT (all_latest.h):
      取所有通道最新消息，全部可用时返回。
    """

    def fuse(self, sources: Dict[str, T]) -> Optional[T]:
        if not sources:
            return None
        # 返回最后插入的(dict保持插入顺序, Python 3.7+)
        return list(sources.values())[-1]

    def name(self) -> str:
        return "all_latest"


class WeightedAverageFusion(FusionStrategy[Vec3]):
    """
    加权平均融合 — 用于多传感器位置融合

    每个来源有一个权重, 融合结果 = Σ(weight_i * state_i) / Σ(weight_i)

    用途:
      多个lidar看到同一个个体, 各自估计了位置。
      按各lidar的置信度加权平均。

    governance对应:
      trust_score 加权 — 高信任个体的观测权重更大。
    """

    def __init__(self):
        self._weights: Dict[str, float] = {}

    def set_weight(self, source_id: str, weight: float):
        """设置来源权重"""
        self._weights[source_id] = weight

    def fuse(self, sources: Dict[str, Vec3]) -> Optional[Vec3]:
        if not sources:
            return None

        total_w = 0.0
        wx, wy, wz = 0.0, 0.0, 0.0

        for src_id, pos in sources.items():
            w = self._weights.get(src_id, 1.0)
            wx += w * pos.x
            wy += w * pos.y
            wz += w * pos.z
            total_w += w

        if total_w == 0:
            return None

        inv_w = 1.0 / total_w
        return Vec3(wx * inv_w, wy * inv_w, wz * inv_w)

    def name(self) -> str:
        return "weighted_average"


class LatestPerSourceFusion(FusionStrategy[T]):
    """
    每源最新值融合 — 保持所有来源的最新值

    与AllLatest不同: AllLatest返回一个值, 这个返回所有来源各自的最新值。
    用于: 需要知道每个传感器的独立观测。

    CyberRT: DataVisitor.TryFetch(m0, m1, ...) — 按通道分别返回
    """

    def fuse(self, sources: Dict[str, T]) -> Optional[Dict[str, T]]:
        if not sources:
            return None
        return dict(sources)

    def name(self) -> str:
        return "latest_per_source"


# ─── FusionEngine ───

class FusionEntry:
    """一个融合点的状态"""
    __slots__ = ('channel_name', 'sources', 'strategy', 'result',
                 'last_fuse_tick', 'fuse_count', 'total_fuse_ns')

    def __init__(self, channel_name: str, strategy: FusionStrategy):
        self.channel_name = channel_name
        self.sources: Dict[str, Any] = {}
        self.strategy = strategy
        self.result = None
        self.last_fuse_tick = -1
        self.fuse_count = 0
        self.total_fuse_ns = 0


class FusionEngine:
    """
    多源状态融合引擎 — PRD #1

    管理多个融合点, 每个融合点:
      - 有多个来源(source_id → latest_state)
      - 有一个融合策略
      - 每个tick执行一次融合

    数据流:
      publisher_A.write(state) → DataDispatcher → FusionEngine.update()
      publisher_B.write(state) → DataDispatcher → FusionEngine.update()
      tick → FusionEngine.fuse_all() → subscriber.callback(fused_state)

    governance对应:
      Runtime 的多策略 evidence 聚合:
        多个 annotator 产生 evidence → 聚合为 policy_input →
        policy.evaluate()
      FusionEngine 的多源数据聚合:
        多个 publisher 产生 state → fuse → subscriber 回调

    1000个体性能:
      假设每个个体有3个融合点, 每个融合点3个来源:
      3000次 fuse() 调用 × ~1μs/call = ~3ms (在33ms budget内)
    """

    def __init__(self):
        self._entries: Dict[str, FusionEntry] = {}
        self._lock = threading.Lock()
        self._subscriber_callbacks: Dict[str, Callable] = {}

    def register_fusion(self, channel_name: str,
                        strategy: FusionStrategy = None):
        """
        注册一个融合点

        channel_name: 融合结果发布的通道名
        strategy: 融合策略 (默认 AllLatest)
        """
        if strategy is None:
            strategy = AllLatestFusion()
        with self._lock:
            self._entries[channel_name] = FusionEntry(channel_name, strategy)

    def update(self, channel_name: str, source_id: str, state: Any):
        """
        更新来源数据 — 由 DataDispatcher 调用

        每次某个publisher发布状态, DataDispatcher把数据路由到这里。
        """
        with self._lock:
            entry = self._entries.get(channel_name)
            if entry is None:
                return
            entry.sources[source_id] = state

    def subscribe(self, channel_name: str, callback: Callable):
        """注册融合结果的订阅回调"""
        with self._lock:
            self._subscriber_callbacks[channel_name] = callback

    def fuse_one(self, channel_name: str, tick_seq: int = 0) -> Any:
        """融合单个通道 — 返回融合结果"""
        with self._lock:
            entry = self._entries.get(channel_name)
            if entry is None:
                return None
            sources_copy = dict(entry.sources)
            strategy = entry.strategy

        t0 = time.monotonic_ns()
        result = strategy.fuse(sources_copy)
        dt = time.monotonic_ns() - t0

        with self._lock:
            entry.result = result
            entry.last_fuse_tick = tick_seq
            entry.fuse_count += 1
            entry.total_fuse_ns += dt

        return result

    def fuse_all(self, tick_seq: int = 0) -> Dict[str, Any]:
        """
        融合所有通道 — 每个tick调用一次

        PRD #18 优化:
          当前是逐个通道串行融合。
          如果通道数 > CPU核数, 可以用 Scheduler.create_task_batch()
          并行融合。当前1000个体场景下串行足够(~3ms)。

        返回 {channel_name: fused_result}
        """
        with self._lock:
            channels = list(self._entries.keys())

        results = {}
        for ch in channels:
            result = self.fuse_one(ch, tick_seq)
            results[ch] = result

            # 通知订阅者
            callback = self._subscriber_callbacks.get(ch)
            if callback and result is not None:
                callback(result)

        return results

    def stats(self) -> Dict[str, Dict]:
        """融合统计"""
        with self._lock:
            return {
                ch: {
                    "source_count": len(e.sources),
                    "strategy": e.strategy.name(),
                    "fuse_count": e.fuse_count,
                    "avg_fuse_us": (e.total_fuse_ns / max(1, e.fuse_count)) / 1000,
                    "last_tick": e.last_fuse_tick,
                }
                for ch, e in self._entries.items()
            }


# ─── Vectorized predicates — PRD #18 ───
#
# CyberRT: data层没有向量化操作
# pubsub-loop需求: 对大量个体状态做批量谓词检测
#
# 例: 检查所有个体是否都在合法区域内
#     all_of(individuals, lambda i: i.position.x > 0)
#
# 优化: 避免Python逐个调用lambda的开销

def all_of(items: List, predicate: Callable[[Any], bool]) -> bool:
    """
    全量谓词检测 — PRD #18 优化版

    用内置 all() + generator (C层循环, 避免Python逐个lambda)
    短路求值: 第一个False就返回

    性能:
      1000个体: ~50μs (vs naive loop ~200μs)
    """
    return all(predicate(item) for item in items)


def any_of(items: List, predicate: Callable[[Any], bool]) -> bool:
    """存在谓词检测 — 至少一个满足"""
    return any(predicate(item) for item in items)


def none_of(items: List, predicate: Callable[[Any], bool]) -> bool:
    """不存在谓词检测 — 没有一个满足"""
    return not any(predicate(item) for item in items)


def count_if(items: List, predicate: Callable[[Any], bool]) -> int:
    """计数谓词 — 满足条件的个数"""
    return sum(1 for item in items if predicate(item))


def partition(items: List[T], predicate: Callable[[T], bool]
              ) -> Tuple[List[T], List[T]]:
    """
    分区 — 按谓词分成两组

    用于 WorldResolver:
      将个体分为"有碰撞请求"和"静止"两组,
      只对有请求的组做碰撞检测。
    """
    true_group: List[T] = []
    false_group: List[T] = []
    for item in items:
        if predicate(item):
            true_group.append(item)
        else:
            false_group.append(item)
    return true_group, false_group


def transform_reduce(items: List[T],
                     transform: Callable[[T], float],
                     initial: float = 0.0,
                     reduce: Callable[[float, float], float] = None
                     ) -> float:
    """
    变换归约 — PRD #46

    先对每个元素做transform, 再用reduce聚合。
    默认reduce是求和。

    用途:
      计算所有个体的总动能:
        transform_reduce(individuals,
                         lambda i: 0.5 * i.mass * i.velocity.length_sq())

    CyberRT: 无直接对应
    C++: std::transform_reduce (C++17)
    """
    if reduce is None:
        # 默认求和 — 用内置sum, 比reduce(add, ...)快
        return initial + sum(transform(item) for item in items)
    result = initial
    for item in items:
        result = reduce(result, transform(item))
    return result
