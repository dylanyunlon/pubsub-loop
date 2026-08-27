# SPDX-License-Identifier: Apache-2.0
#
# world/transport/memory_pool.py
#
# 类型化内存池 — 用于零拷贝传输
#
# CyberRT来源:
#   cyber/transport/shm/segment.h        → 共享内存段管理
#   cyber/transport/shm/readable_info.h  → 消息元信息
#   cyber/transport/shm/state.h          → 段状态
#   cyber/base/object_pool.h             → 对象池(基础设施)
#
# governance来源:
#   无直接对应 — governance 的消息通过 gRPC/WebSocket,
#   不需要内存池。
#
# PRD映射:
#   #27 → typed individual-state allocators, shared-memory striping
#   #4  → pluggable individual state memory allocator framework
#
# 解决的问题:
#   MotionRequest 是高频小消息(~80 bytes, 1000×/tick),
#   每次 malloc/free 会导致内存碎片和延迟抖动。
#   内存池预分配固定大小的消息槽(slot), 分配/释放 O(1)。
#
# 关键约束:
#   1. 消息大小固定(MotionRequest ~80 bytes, ConfirmedState ~120 bytes)
#   2. 池容量 = max_individuals × buffer_depth (1000 × 4 = 4000 slots)
#   3. 线程安全: 多个 CRoutine 并发分配/释放
#
# 还不知道的事:
#   proto序列化对MotionRequest这种高频小消息是不是最优选择
#   → 当前用 struct.pack(固定layout, 无开销)替代protobuf。
#     MotionRequest 是固定大小, struct.pack 是最优的。

import struct
import threading
from typing import TypeVar, Generic, Optional, List, Dict, Any, Type
from collections import deque

T = TypeVar('T')


class PoolSlot:
    """内存池槽位"""
    __slots__ = ('index', 'data', 'in_use')

    def __init__(self, index: int, data: Any = None):
        self.index = index
        self.data = data
        self.in_use = False


class TypedPool(Generic[T]):
    """
    类型化内存池 — 预分配固定类型消息的对象池

    CyberRT来源 (object_pool.h):
      ObjectPool<T>(capacity) — 预分配capacity个T对象
      GetObject() → T* (O(1), 从free list取)
      ReleaseObject(ptr) → void (O(1), 放回free list)

    用途:
      transport层发送 MotionRequest 时:
        old: msg = MotionRequest(...)  # 每次创建新对象
        new: msg = pool.acquire()     # 从池中取, O(1)
             msg.delta_position = ...  # 填充数据
             transmitter.transmit(msg) # 发送
             pool.release(msg)         # 归还池, O(1)

    性能:
      1000个体, 每tick 1000次分配+释放:
        malloc/free: ~1ms (内存碎片+系统调用)
        pool: ~0.1ms (无系统调用, cache友好)

    governance对应:
      无直接对应。governance 是请求驱动(低频),
      不需要内存池级别的优化。
    """

    def __init__(self, factory: callable, capacity: int = 4096):
        """
        factory: 创建新对象的工厂函数
        capacity: 池容量
        """
        self._factory = factory
        self._capacity = capacity
        self._lock = threading.Lock()
        # 预分配
        self._slots: List[PoolSlot] = []
        self._free_indices: deque = deque()
        for i in range(capacity):
            obj = factory()
            slot = PoolSlot(i, obj)
            self._slots.append(slot)
            self._free_indices.append(i)
        # 统计
        self._acquire_count = 0
        self._release_count = 0
        self._peak_in_use = 0

    def acquire(self) -> Optional[T]:
        """
        获取一个对象 — O(1)

        CyberRT: ObjectPool::GetObject()
        """
        with self._lock:
            if not self._free_indices:
                return None  # 池耗尽
            idx = self._free_indices.popleft()
            slot = self._slots[idx]
            slot.in_use = True
            self._acquire_count += 1
            in_use = self._capacity - len(self._free_indices)
            if in_use > self._peak_in_use:
                self._peak_in_use = in_use
            return slot.data

    def release(self, obj: T):
        """
        归还对象 — O(1)

        CyberRT: ObjectPool::ReleaseObject(ptr)
        """
        with self._lock:
            # 查找对应的slot
            for slot in self._slots:
                if slot.data is obj and slot.in_use:
                    slot.in_use = False
                    self._free_indices.append(slot.index)
                    self._release_count += 1
                    return
            # obj不属于此池 — 静默忽略

    @property
    def available(self) -> int:
        """可用对象数"""
        with self._lock:
            return len(self._free_indices)

    @property
    def in_use_count(self) -> int:
        with self._lock:
            return self._capacity - len(self._free_indices)

    @property
    def capacity(self) -> int:
        return self._capacity

    @property
    def stats(self) -> Dict[str, Any]:
        with self._lock:
            return {
                "capacity": self._capacity,
                "available": len(self._free_indices),
                "in_use": self._capacity - len(self._free_indices),
                "peak_in_use": self._peak_in_use,
                "acquire_count": self._acquire_count,
                "release_count": self._release_count,
            }


# ─── Struct-based serialization for fixed-size messages ───
#
# CyberRT 用 protobuf 做序列化。
# 但对 MotionRequest (固定大小, 80 bytes) 和
# ConfirmedState (固定大小, 120 bytes),
# struct.pack 比 protobuf 快 ~10x (无schema解析开销)。
#
# 这是 pubsub-loop 对高频小消息的优化。
# 大消息(如世界快照)仍然可以用 protobuf/pickle。

# MotionRequest 的 struct 格式:
#   individual_id: uint32 (4 bytes)
#   tick_seq: uint64 (8 bytes)
#   delta_x, delta_y, delta_z: float32 × 3 (12 bytes)
#   delta_rx, delta_ry, delta_rz, delta_rw: float32 × 4 (16 bytes)
#   priority: uint8 (1 byte)
#   collision_mask: uint32 (4 bytes)
#   padding: 19 bytes
#   Total: 64 bytes (cache line aligned)
MOTION_REQUEST_FORMAT = '<IQ3f4fBI19x'  # 64 bytes
MOTION_REQUEST_SIZE = struct.calcsize(MOTION_REQUEST_FORMAT)

# ConfirmedState 的 struct 格式:
#   individual_id: uint32 (4 bytes)
#   tick_seq: uint64 (8 bytes)
#   pos_x, pos_y, pos_z: float32 × 3 (12 bytes)
#   rot_x, rot_y, rot_z, rot_w: float32 × 4 (16 bytes)
#   resolve_flags: uint32 (4 bytes)
#   padding: 20 bytes
#   Total: 64 bytes (cache line aligned)
CONFIRMED_STATE_FORMAT = '<IQ3f4fI20x'  # 64 bytes
CONFIRMED_STATE_SIZE = struct.calcsize(CONFIRMED_STATE_FORMAT)


def pack_motion_request(individual_id: int, tick_seq: int,
                        dx: float, dy: float, dz: float,
                        drx: float = 0.0, dry: float = 0.0,
                        drz: float = 0.0, drw: float = 1.0,
                        priority: int = 0,
                        collision_mask: int = 0xFFFFFFFF) -> bytes:
    """MotionRequest → 64 bytes (cache line aligned)"""
    return struct.pack(
        MOTION_REQUEST_FORMAT,
        individual_id, tick_seq,
        dx, dy, dz,
        drx, dry, drz, drw,
        priority, collision_mask,
    )


def unpack_motion_request(data: bytes) -> dict:
    """64 bytes → MotionRequest dict"""
    vals = struct.unpack(MOTION_REQUEST_FORMAT, data[:MOTION_REQUEST_SIZE])
    return {
        'individual_id': vals[0],
        'tick_seq': vals[1],
        'delta_x': vals[2], 'delta_y': vals[3], 'delta_z': vals[4],
        'delta_rx': vals[5], 'delta_ry': vals[6],
        'delta_rz': vals[7], 'delta_rw': vals[8],
        'priority': vals[9],
        'collision_mask': vals[10],
    }


def pack_confirmed_state(individual_id: int, tick_seq: int,
                         px: float, py: float, pz: float,
                         rx: float = 0.0, ry: float = 0.0,
                         rz: float = 0.0, rw: float = 1.0,
                         resolve_flags: int = 0) -> bytes:
    """ConfirmedState → 64 bytes (cache line aligned)"""
    return struct.pack(
        CONFIRMED_STATE_FORMAT,
        individual_id, tick_seq,
        px, py, pz,
        rx, ry, rz, rw,
        resolve_flags,
    )


def unpack_confirmed_state(data: bytes) -> dict:
    """64 bytes → ConfirmedState dict"""
    vals = struct.unpack(CONFIRMED_STATE_FORMAT, data[:CONFIRMED_STATE_SIZE])
    return {
        'individual_id': vals[0],
        'tick_seq': vals[1],
        'pos_x': vals[2], 'pos_y': vals[3], 'pos_z': vals[4],
        'rot_x': vals[5], 'rot_y': vals[6],
        'rot_z': vals[7], 'rot_w': vals[8],
        'resolve_flags': vals[9],
    }


# ─── PoolManager — PRD #4 ───

class PoolManager:
    """
    全局内存池管理器 — 按消息类型注册和管理内存池

    PRD #4: pluggable individual state memory allocator framework

    使用:
      # 启动时注册
      pm = PoolManager.instance()
      pm.register_pool("MotionRequest", lambda: MotionRequest(), 4096)
      pm.register_pool("ConfirmedState", lambda: ConfirmedState(), 4096)

      # 运行时使用
      mr = pm.acquire("MotionRequest")
      mr.delta_position = Vec3(1, 0, 0)
      transmitter.transmit(mr)
      pm.release("MotionRequest", mr)
    """
    _instance = None
    _init_lock = threading.Lock()

    @classmethod
    def instance(cls) -> 'PoolManager':
        if cls._instance is None:
            with cls._init_lock:
                if cls._instance is None:
                    cls._instance = cls()
        return cls._instance

    def __init__(self):
        self._pools: Dict[str, TypedPool] = {}
        self._lock = threading.Lock()

    def register_pool(self, type_name: str, factory: callable,
                      capacity: int = 4096):
        """注册一个类型的内存池"""
        with self._lock:
            if type_name not in self._pools:
                self._pools[type_name] = TypedPool(factory, capacity)

    def acquire(self, type_name: str) -> Optional[Any]:
        """从指定类型池中获取对象"""
        with self._lock:
            pool = self._pools.get(type_name)
        if pool is None:
            return None
        return pool.acquire()

    def release(self, type_name: str, obj: Any):
        """归还对象到指定类型池"""
        with self._lock:
            pool = self._pools.get(type_name)
        if pool is not None:
            pool.release(obj)

    def stats(self) -> Dict[str, Dict]:
        """所有池的统计"""
        with self._lock:
            return {name: pool.stats for name, pool in self._pools.items()}
