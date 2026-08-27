# SPDX-License-Identifier: Apache-2.0
#
# world/base/primitives.py
#
# 基础并发原语 — 从 CyberRT cyber/base 移植
#
# CyberRT来源:
#   cyber/base/bounded_queue.h  → BoundedQueue (lock-free SPSC/MPSC ring buffer)
#   cyber/base/atomic_hash_map.h → AtomicHashMap (lock-free hash map)
#   cyber/base/signal.h         → Signal/Slot (观察者模式, Component回调注册)
#   cyber/base/thread_pool.h    → ThreadPool (固定大小线程池)
#
# governance来源:
#   policy-engine/core: 无直接对应的底层原语，
#   但 Runtime 的 telemetry sink 和 event dispatch 依赖类似的并发队列。
#
# 关键约束:
#   1. BoundedQueue 是 transport 层的基础 — SHM transmitter 用它做消息缓冲
#   2. AtomicHashMap 是 service_discovery 的基础 — 存储 channel→writer 映射
#   3. Signal 是 Component 回调注册的基础 — Reader 的回调通过 Signal 连接
#
# Python适配:
#   CyberRT 用 lock-free C++ 实现 (CAS操作)。
#   Python 不适合 lock-free (GIL限制)，用 threading.Lock 替代，
#   保持接口语义一致。PRD #19: 对齐字节数工具在此实现。

import threading
from typing import TypeVar, Generic, Optional, Callable, List, Dict
from collections import deque

T = TypeVar('T')


class BoundedQueue(Generic[T]):
    """
    有界队列 — 固定容量的FIFO消息缓冲

    CyberRT来源 (cyber/base/bounded_queue.h):
      lock-free SPSC ring buffer, 用于 transport 层消息传递。
      Enqueue() → CAS写指针
      Dequeue() → CAS读指针
      WaitEnqueue() → busy-wait版本

    Python适配:
      用 deque(maxlen=N) + Lock 实现。
      语义保持: 满时Enqueue返回False(不阻塞), WaitEnqueue阻塞。

    用途:
      transport::ShmTransmitter 的消息发送缓冲
      data::CacheBuffer 的历史状态存储
      scheduler::Processor 的任务队列
    """

    def __init__(self, capacity: int):
        self._capacity = capacity
        self._queue: deque = deque()
        self._lock = threading.Lock()
        self._not_empty = threading.Condition(self._lock)
        self._not_full = threading.Condition(self._lock)

    def enqueue(self, item: T) -> bool:
        """非阻塞入队 — 满时返回False

        CyberRT: bool Enqueue(const T& element) — CAS尝试一次
        """
        with self._lock:
            if len(self._queue) >= self._capacity:
                return False
            self._queue.append(item)
            self._not_empty.notify()
            return True

    def wait_enqueue(self, item: T, timeout_s: float = 1.0) -> bool:
        """阻塞入队 — 等待空间可用

        CyberRT: bool WaitEnqueue(const T& element) — busy-wait
        """
        with self._not_full:
            while len(self._queue) >= self._capacity:
                if not self._not_full.wait(timeout=timeout_s):
                    return False
            self._queue.append(item)
            self._not_empty.notify()
            return True

    def dequeue(self) -> Optional[T]:
        """非阻塞出队 — 空时返回None

        CyberRT: bool Dequeue(T* element) — CAS尝试一次
        """
        with self._lock:
            if not self._queue:
                return None
            item = self._queue.popleft()
            self._not_full.notify()
            return item

    def wait_dequeue(self, timeout_s: float = 1.0) -> Optional[T]:
        """阻塞出队 — 等待数据可用"""
        with self._not_empty:
            while not self._queue:
                if not self._not_empty.wait(timeout=timeout_s):
                    return None
            item = self._queue.popleft()
            self._not_full.notify()
            return item

    @property
    def size(self) -> int:
        with self._lock:
            return len(self._queue)

    @property
    def capacity(self) -> int:
        return self._capacity

    @property
    def empty(self) -> bool:
        with self._lock:
            return len(self._queue) == 0

    @property
    def full(self) -> bool:
        with self._lock:
            return len(self._queue) >= self._capacity


class AtomicHashMap(Generic[T]):
    """
    并发安全哈希表 — 支持并发读写

    CyberRT来源 (cyber/base/atomic_hash_map.h):
      lock-free open-addressing hash map
      Insert(key, value) → CAS
      Get(key) → atomic load
      用于 service_discovery 的 channel→endpoint 映射

    Python适配:
      dict + RWLock 实现。

    用途:
      ChannelManager 的 channel_name → Writer/Reader 映射
      ServiceDiscovery 的拓扑管理
      WorldResolver 的 individual_id → ConfirmedState 映射
    """

    def __init__(self):
        self._data: Dict = {}
        self._lock = threading.RLock()

    def insert(self, key, value: T) -> bool:
        """插入 — key已存在时返回False

        CyberRT: bool Insert(K key, V value)
        """
        with self._lock:
            if key in self._data:
                return False
            self._data[key] = value
            return True

    def set(self, key, value: T):
        """设置 — key已存在时覆盖"""
        with self._lock:
            self._data[key] = value

    def get(self, key) -> Optional[T]:
        """获取 — key不存在时返回None

        CyberRT: bool Get(K key, V* value)
        """
        with self._lock:
            return self._data.get(key)

    def remove(self, key) -> bool:
        """删除"""
        with self._lock:
            if key in self._data:
                del self._data[key]
                return True
            return False

    def contains(self, key) -> bool:
        with self._lock:
            return key in self._data

    def keys(self) -> list:
        with self._lock:
            return list(self._data.keys())

    def values(self) -> list:
        with self._lock:
            return list(self._data.values())

    def items(self) -> list:
        with self._lock:
            return list(self._data.items())

    @property
    def size(self) -> int:
        with self._lock:
            return len(self._data)


class Signal:
    """
    信号/槽 — 观察者模式

    CyberRT来源 (cyber/base/signal.h):
      Signal<Args...> — 多播信号
      Connection = signal.Connect(slot)
      signal(args...) → 调用所有已连接slot

    用途:
      Reader 的回调注册: reader.signal_.Connect(callback)
      Component 的 Init/Proc 绑定
      transport::Notifier 的通知机制

    governance对应:
      HypervisorEventBus.subscribe(type, handler) — 按类型订阅
      Signal 更底层: 无类型过滤，所有连接的slot都会被调用
    """

    def __init__(self):
        self._slots: List[Callable] = []
        self._lock = threading.Lock()

    def connect(self, slot: Callable) -> int:
        """连接slot — 返回connection id

        CyberRT: Connection Connect(const Slot& slot)
        """
        with self._lock:
            conn_id = len(self._slots)
            self._slots.append(slot)
            return conn_id

    def disconnect(self, conn_id: int):
        """断开连接"""
        with self._lock:
            if 0 <= conn_id < len(self._slots):
                self._slots[conn_id] = None

    def emit(self, *args, **kwargs):
        """触发信号 — 调用所有已连接slot

        CyberRT: void operator()(Args... args)
        """
        with self._lock:
            slots = list(self._slots)
        for slot in slots:
            if slot is not None:
                slot(*args, **kwargs)

    def __call__(self, *args, **kwargs):
        self.emit(*args, **kwargs)

    @property
    def slot_count(self) -> int:
        with self._lock:
            return sum(1 for s in self._slots if s is not None)


class ThreadPool:
    """
    固定大小线程池

    CyberRT来源 (cyber/base/thread_pool.h):
      ThreadPool(num_threads) — 固定大小
      Enqueue(func) → future
      用于 non-realtime 任务 (日志、统计)

    scheduler模块用 CRoutine 做实时任务调度，
    ThreadPool 只用于非实时后台工作。
    """

    def __init__(self, num_threads: int):
        import concurrent.futures
        self._pool = concurrent.futures.ThreadPoolExecutor(
            max_workers=num_threads,
            thread_name_prefix="world_pool"
        )

    def submit(self, func: Callable, *args, **kwargs):
        """提交任务 — 返回Future"""
        return self._pool.submit(func, *args, **kwargs)

    def shutdown(self, wait: bool = True):
        self._pool.shutdown(wait=wait)


def get_aligned_layout(byte_size: int, alignment: int = 64) -> int:
    """
    计算对齐字节数 — PRD #19

    CyberRT来源: 对齐到cache line(64 bytes)，
    用于 SHM transport 的零拷贝消息布局。

    >>> get_aligned_layout(100, 64)
    128
    >>> get_aligned_layout(64, 64)
    64
    """
    if alignment <= 0:
        return byte_size
    return ((byte_size + alignment - 1) // alignment) * alignment
