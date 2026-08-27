# SPDX-License-Identifier: Apache-2.0
#
# world/data/data_core.py
#
# 数据层 — 从 CyberRT cyber/data 移植
#
# CyberRT来源:
#   cyber/data/cache_buffer.h    → CacheBuffer (固定容量的历史状态环形缓冲)
#   cyber/data/data_dispatcher.h → DataDispatcher (通道数据分发器)
#   cyber/data/data_visitor.h    → DataVisitor (多通道数据融合访问)
#   cyber/data/channel_buffer.h  → ChannelBuffer (per-channel 缓冲 + 融合)
#   cyber/data/fusion/all_latest.h → AllLatest (取所有通道最新值)
#
# 新增:
#   SpatialHash — 世界专用空间索引 (从 world_resolver 的 broadphase 提取)
#   用于大规模个体碰撞粗筛。PRD #1455: sweep-and-prune / spatial hash
#
# governance来源:
#   SharedSessionObject — VFS状态 (对应 CacheBuffer 的一致快照)
#   AgentMesh.participants — 对应 DataVisitor 的多通道融合
#
# 关键约束:
#   1. CacheBuffer 是固定容量环形缓冲 — 满时覆盖最旧数据
#   2. DataDispatcher 是全局单例 — 所有通道的数据分发通过它
#   3. DataVisitor 是 Component 的数据入口 — 多通道最新值融合
#   4. SpatialHash 是 WorldResolver broadphase 的核心 —
#      替换 O(n²) 为 O(n) 空间哈希碰撞粗筛

import threading
from collections import deque
from typing import TypeVar, Generic, Optional, Callable, Dict, List, Tuple, Set
from world.proto.world_types import Vec3

T = TypeVar('T')


class CacheBuffer(Generic[T]):
    """
    固定容量环形缓冲 — 历史状态存储

    CyberRT来源 (cyber/data/cache_buffer.h):
      CacheBuffer(capacity) — 固定大小
      Fill(msg) → 写入最新，超容量覆盖最旧
      Front/Back — 最新/最旧
      at(index) — 按索引访问

    用途:
      1. data::ChannelBuffer 内部用它存储每个通道的历史消息
      2. WorldResolver 可用它保存最近N个tick的confirmed_state快照
      3. 渲染层用它做状态插值(最近2个状态之间线性插值)

    governance对应:
      类似 DeltaEngine 的历史 delta 存储 — 固定窗口内的状态变更。
    """

    def __init__(self, capacity: int):
        self._capacity = capacity
        self._buffer: deque = deque(maxlen=capacity)
        self._lock = threading.Lock()

    def fill(self, msg: T) -> bool:
        """写入最新值 — 满时覆盖最旧

        CyberRT: void Fill(const std::shared_ptr<T>& msg)
        """
        with self._lock:
            self._buffer.append(msg)
        return True

    def front(self) -> Optional[T]:
        """最新值 (队尾)"""
        with self._lock:
            return self._buffer[-1] if self._buffer else None

    def back(self) -> Optional[T]:
        """最旧值 (队头)"""
        with self._lock:
            return self._buffer[0] if self._buffer else None

    def at(self, index: int) -> Optional[T]:
        """按索引访问"""
        with self._lock:
            if 0 <= index < len(self._buffer):
                return self._buffer[index]
            return None

    def latest(self, n: int = 1) -> List[T]:
        """最近n个值"""
        with self._lock:
            items = list(self._buffer)
            return items[-n:] if n <= len(items) else items

    @property
    def size(self) -> int:
        with self._lock:
            return len(self._buffer)

    @property
    def capacity(self) -> int:
        return self._capacity

    @property
    def empty(self) -> bool:
        with self._lock:
            return len(self._buffer) == 0

    def snapshot(self) -> List[T]:
        """一致快照 — governance: SharedSessionObject.snapshot()"""
        with self._lock:
            return list(self._buffer)


class DataDispatcher:
    """
    全局数据分发器 — 单例

    CyberRT来源 (cyber/data/data_dispatcher.h):
      Dispatch(channel_id, msg) → 通知所有该通道的 DataVisitor
      AddBuffer(channel_id, buffer) → 注册缓冲区
      全局单例: 所有通道的数据流通过DataDispatcher路由

    governance对应:
      HypervisorEventBus — 但EventBus按类型分发，DataDispatcher按通道分发。

    与 transport::IntraDispatcher 的区别:
      IntraDispatcher: transport层 — 负责消息的物理传输
      DataDispatcher:  data层 — 负责消息的逻辑分发到DataVisitor
      一条消息: transport → DataDispatcher → DataVisitor → Component::Proc()
    """
    _instance = None
    _lock = threading.Lock()

    @classmethod
    def instance(cls) -> 'DataDispatcher':
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = cls()
        return cls._instance

    def __init__(self):
        self._buffers: Dict[str, List[CacheBuffer]] = {}
        self._notifiers: Dict[str, List[Callable]] = {}
        self._lock = threading.Lock()

    def add_buffer(self, channel_name: str, buffer: CacheBuffer):
        """注册缓冲区到通道

        CyberRT: void AddBuffer(const ChannelBuffer<T>& buffer)
        """
        with self._lock:
            if channel_name not in self._buffers:
                self._buffers[channel_name] = []
            self._buffers[channel_name].append(buffer)

    def add_notifier(self, channel_name: str, notifier: Callable):
        """注册通知回调 — DataVisitor注册的"有新数据"通知"""
        with self._lock:
            if channel_name not in self._notifiers:
                self._notifiers[channel_name] = []
            self._notifiers[channel_name].append(notifier)

    def dispatch(self, channel_name: str, msg):
        """分发消息到所有缓冲区和通知回调

        CyberRT: void Dispatch(uint64_t channel_id, const T& msg)
        """
        with self._lock:
            buffers = list(self._buffers.get(channel_name, []))
            notifiers = list(self._notifiers.get(channel_name, []))

        for buf in buffers:
            buf.fill(msg)
        for notify in notifiers:
            notify()


class DataVisitor:
    """
    多通道数据融合访问器

    CyberRT来源 (cyber/data/data_visitor.h):
      DataVisitor(channel_configs) — 订阅多个通道
      TryFetch(msg0, msg1, ...) — 尝试获取所有通道的最新数据
      RegisterNotifyCallback(callback) — 注册"有新数据"回调

    用途:
      Component<M0, M1, ...>::Init() 时创建 DataVisitor,
      订阅多个通道，当所有通道都有新数据时触发 Proc()。

    governance对应:
      类似 Runtime 的 policy_input 构建 —
      从多个来源(manifest, snapshot, annotations)收集数据，
      合并为一个 policy_input 传给 policy.evaluate()。

    fusion策略:
      AllLatest — 取所有通道的最新值(默认)
      CyberRT还有其他策略(AllShortest等)，目前只实现AllLatest。
    """

    def __init__(self, channel_names: List[str],
                 buffer_capacity: int = 16):
        self._channels = channel_names
        self._buffers: Dict[str, CacheBuffer] = {}
        self._notify_callback: Optional[Callable] = None

        dispatcher = DataDispatcher.instance()
        for ch in channel_names:
            buf = CacheBuffer(buffer_capacity)
            self._buffers[ch] = buf
            dispatcher.add_buffer(ch, buf)
            dispatcher.add_notifier(ch, self._on_channel_data)

    def _on_channel_data(self):
        """某个通道有新数据 — 检查是否所有通道都有数据"""
        if self._notify_callback and self._all_have_data():
            self._notify_callback()

    def _all_have_data(self) -> bool:
        """AllLatest策略: 所有通道都至少有一条数据"""
        return all(not buf.empty for buf in self._buffers.values())

    def try_fetch(self) -> Optional[Dict[str, any]]:
        """
        尝试获取所有通道最新数据

        CyberRT: bool TryFetch(std::shared_ptr<M0>& m0, ...)

        返回 {channel_name: latest_msg} 或 None(某个通道没数据)
        """
        result = {}
        for ch, buf in self._buffers.items():
            latest = buf.front()
            if latest is None:
                return None
            result[ch] = latest
        return result

    def register_notify_callback(self, callback: Callable):
        """注册"所有通道都有新数据"的回调

        CyberRT: void RegisterNotifyCallback(std::function<void()>)
        """
        self._notify_callback = callback

    def get_buffer(self, channel_name: str) -> Optional[CacheBuffer]:
        return self._buffers.get(channel_name)


# ─── SpatialHash — 世界专用空间索引 ───

class SpatialHash:
    """
    空间哈希 — 碰撞粗筛加速

    CyberRT来源: 无直接对应，但PRD #1452/#1455要求broadphase使用空间索引。
    从WorldResolver的O(n²) broadphase提取为独立数据结构。

    原理:
      将3D空间划分为固定大小的格子(cell)。
      每个格子维护一个个体列表。
      碰撞检测时只检查同一格子和相邻格子内的个体对。

    复杂度:
      O(n²) pairwise → O(n * k) 其中 k 是每个格子平均个体数。
      对于"1000个个体均匀分布"的场景，k ≈ 1~5, 性能提升 >100x。

    用于 WorldResolver._broadphase():
      old: O(n²) pairwise check
      new: SpatialHash.query_pairs() → 只返回相邻个体对

    service_discovery对应:
      TopologyManager 的"节点发现"也是拓扑索引，
      但是逻辑拓扑(谁连接谁)，不是空间拓扑(谁在谁旁边)。
      对于"世界中有1000个个体"的场景：
      - SpatialHash: 够用 — O(n) 插入/更新, 空间占用线性
      - TopologyManager: 需要评估 — 1000个node的全连接图可能过大
        PRD待确认是否需要分层拓扑管理
    """

    def __init__(self, cell_size: float = 2.0):
        self._cell_size = cell_size
        self._inv_cell = 1.0 / cell_size
        self._grid: Dict[Tuple[int, int, int], List[int]] = {}
        self._positions: Dict[int, Vec3] = {}

    def _cell_key(self, pos: Vec3) -> Tuple[int, int, int]:
        return (
            int(pos.x * self._inv_cell) if pos.x >= 0
            else int(pos.x * self._inv_cell) - 1,
            int(pos.y * self._inv_cell) if pos.y >= 0
            else int(pos.y * self._inv_cell) - 1,
            int(pos.z * self._inv_cell) if pos.z >= 0
            else int(pos.z * self._inv_cell) - 1,
        )

    def clear(self):
        """清空 — 每个tick重建"""
        self._grid.clear()
        self._positions.clear()

    def insert(self, individual_id: int, position: Vec3):
        """插入个体"""
        key = self._cell_key(position)
        if key not in self._grid:
            self._grid[key] = []
        self._grid[key].append(individual_id)
        self._positions[individual_id] = position

    def query_neighbors(self, individual_id: int) -> List[int]:
        """查询一个个体的相邻个体 — 搜索所在格子和26个相邻格子"""
        pos = self._positions.get(individual_id)
        if pos is None:
            return []
        cx, cy, cz = self._cell_key(pos)
        neighbors = []
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    key = (cx + dx, cy + dy, cz + dz)
                    for nid in self._grid.get(key, []):
                        if nid != individual_id:
                            neighbors.append(nid)
        return neighbors

    def query_pairs(self) -> List[Tuple[int, int]]:
        """
        查询所有可能碰撞的个体对

        只返回同格子和相邻格子内的个体对。
        去重: 只返回 (a, b) 其中 a < b。

        这是 WorldResolver._broadphase() 的核心调用。
        """
        pairs: Set[Tuple[int, int]] = set()
        for key, ids in self._grid.items():
            cx, cy, cz = key
            # 收集本格子 + 相邻格子的所有个体
            nearby: List[int] = []
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        neighbor_key = (cx + dx, cy + dy, cz + dz)
                        nearby.extend(self._grid.get(neighbor_key, []))

            # 去重: 对于本格子内的每个个体，与nearby列表配对
            for a in ids:
                for b in nearby:
                    if a < b:
                        pairs.add((a, b))
        return list(pairs)

    def query_radius(self, center: Vec3, radius: float) -> List[int]:
        """
        查询半径内的所有个体

        先用空间哈希缩小范围，再用精确距离过滤。
        """
        result = []
        r_sq = radius * radius
        cells_r = int(radius * self._inv_cell) + 1
        cx, cy, cz = self._cell_key(center)

        for dx in range(-cells_r, cells_r + 1):
            for dy in range(-cells_r, cells_r + 1):
                for dz in range(-cells_r, cells_r + 1):
                    key = (cx + dx, cy + dy, cz + dz)
                    for iid in self._grid.get(key, []):
                        pos = self._positions[iid]
                        dist_sq = (
                            (pos.x - center.x) ** 2 +
                            (pos.y - center.y) ** 2 +
                            (pos.z - center.z) ** 2
                        )
                        if dist_sq <= r_sq:
                            result.append(iid)
        return result

    @property
    def individual_count(self) -> int:
        return len(self._positions)

    @property
    def cell_count(self) -> int:
        return len(self._grid)
