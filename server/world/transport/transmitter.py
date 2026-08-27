# SPDX-License-Identifier: Apache-2.0
#
# world/transport/transmitter.py
#
# 传输层 — 从 CyberRT cyber/transport 移植
#
# CyberRT来源:
#   cyber/transport/transmitter/transmitter.h     → Transmitter 基类
#   cyber/transport/transmitter/intra_transmitter.h → IntraTransmitter (零拷贝)
#   cyber/transport/transmitter/shm_transmitter.h   → ShmTransmitter (共享内存)
#   cyber/transport/transmitter/rtps_transmitter.h  → RtpsTransmitter (跨节点DDS)
#   cyber/transport/transmitter/hybrid_transmitter.h → HybridTransmitter (自动路由)
#   cyber/transport/transport.h                     → Transport (工厂)
#
# governance来源:
#   policy-engine 无直接传输层对应。
#   gRPC/WebSocket transport 在 agent-governance-toolkit 是 integration 层，
#   不在 core policy-engine 里。
#
# PRD映射:
#   #0  → RTPS/SHM统一
#   #17 → 按拓扑距离自动路由
#   #27 → memory_pool typed allocators
#
# 关键约束:
#   1. IntraTransmitter: 进程内零拷贝 — 直接传Python引用
#   2. ShmTransmitter: 共享内存 — multiprocessing.shared_memory
#   3. HybridTransmitter: 按拓扑距离自动选择 intra/shm
#   4. "个体运动申请→世界确认"的消息 MotionRequest 是高频小消息,
#      transport层需要对这种pattern做优化
#
# 还不知道的事:
#   - RTPS/SHM在"个体运动申请→世界确认"pattern下延迟表现
#     → 目前SHM实现是Python multiprocessing.shared_memory，
#       不是CyberRT的posix_shm+信号通知，延迟会高于C++版本。
#       先实现功能正确性，性能优化留给后续FFI桥接。
#   - proto序列化对MotionRequest这种高频小消息是不是最优
#     → 当前用pickle/struct，不用protobuf。
#       MotionRequest 固定大小(~80 bytes)，直接struct.pack更快。

import struct
import threading
import time
from abc import ABC, abstractmethod
from enum import Enum, auto
from typing import TypeVar, Generic, Optional, Callable, Dict, List, Any
from multiprocessing import shared_memory

from world.base.primitives import BoundedQueue, Signal

T = TypeVar('T')


class TransportMode(Enum):
    """传输模式 — 对应 CyberRT OptionalMode"""
    INTRA  = "intra"    # 进程内零拷贝
    SHM    = "shm"      # 共享内存
    HYBRID = "hybrid"   # 自动选择


class MessageInfo:
    """
    消息元信息 — 对应 CyberRT transport::MessageInfo

    CyberRT:
      sender_id, seq_num, send_time, spare_id
    """
    __slots__ = ('sender_id', 'seq_num', 'send_time_ns', 'channel_name')

    def __init__(self, sender_id: int = 0, channel_name: str = ""):
        self.sender_id = sender_id
        self.seq_num = 0
        self.send_time_ns = 0
        self.channel_name = channel_name


class Transmitter(ABC, Generic[T]):
    """
    传输器基类 — 对应 CyberRT transport::Transmitter<M>

    CyberRT (transmitter.h):
      Enable() / Disable() — 启用/禁用
      Transmit(msg, msg_info) — 发送消息
      NextSeqNum() — 递增序列号
      AcquireMessage(msg) — 获取消息(用于pooling)

    子类实现不同传输模式:
      IntraTransmitter — 进程内直接传引用
      ShmTransmitter   — 通过共享内存传序列化数据
      HybridTransmitter — 根据拓扑自动选择
    """

    def __init__(self, channel_name: str, mode: TransportMode):
        self.channel_name = channel_name
        self.mode = mode
        self._enabled = False
        self._seq_num = 0
        self._msg_info = MessageInfo(
            sender_id=id(self),
            channel_name=channel_name,
        )
        # 统计
        self._transmit_count = 0
        self._total_latency_ns = 0

    @abstractmethod
    def enable(self):
        """CyberRT: Enable()"""
        self._enabled = True

    @abstractmethod
    def disable(self):
        """CyberRT: Disable()"""
        self._enabled = False

    @abstractmethod
    def transmit(self, msg: T) -> bool:
        """CyberRT: Transmit(msg, msg_info)"""
        ...

    def next_seq_num(self) -> int:
        """CyberRT: NextSeqNum()"""
        self._seq_num += 1
        return self._seq_num

    @property
    def is_enabled(self) -> bool:
        return self._enabled

    @property
    def stats(self) -> Dict[str, Any]:
        return {
            "mode": self.mode.value,
            "channel": self.channel_name,
            "transmit_count": self._transmit_count,
            "avg_latency_us": (self._total_latency_ns / max(1, self._transmit_count)) / 1000,
        }


class Receiver(ABC, Generic[T]):
    """
    接收器基类 — 对应 CyberRT transport::Receiver<M>

    CyberRT:
      Enable() / Disable()
      OnNewMessage(channel_id, msg, msg_info) — 收到消息回调
    """

    def __init__(self, channel_name: str, callback: Callable[[T], None]):
        self.channel_name = channel_name
        self._callback = callback
        self._enabled = False
        self._receive_count = 0

    @abstractmethod
    def enable(self):
        self._enabled = True

    @abstractmethod
    def disable(self):
        self._enabled = False

    def on_new_message(self, msg: T):
        """收到消息 — 调用回调"""
        if self._enabled and self._callback:
            self._receive_count += 1
            self._callback(msg)


# ─── IntraTransmitter/Receiver ───

class IntraDispatcher:
    """
    进程内消息分发器 — 单例

    CyberRT来源 (cyber/transport/dispatcher/intra_dispatcher.h):
      OnMessage(channel_id, msg, msg_info) → 分发给所有该channel的receiver
      AddListener(channel_id, callback)

    进程内: 零拷贝 — 直接传Python引用，不序列化。
    这是最快的传输模式，用于同进程内的个体通信。
    """
    _instance = None
    _lock = threading.Lock()

    @classmethod
    def instance(cls) -> 'IntraDispatcher':
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = cls()
        return cls._instance

    def __init__(self):
        self._listeners: Dict[str, List[Callable]] = {}
        self._lock = threading.Lock()

    def add_listener(self, channel_name: str, callback: Callable):
        with self._lock:
            if channel_name not in self._listeners:
                self._listeners[channel_name] = []
            self._listeners[channel_name].append(callback)

    def remove_listener(self, channel_name: str, callback: Callable):
        with self._lock:
            if channel_name in self._listeners:
                self._listeners[channel_name] = [
                    cb for cb in self._listeners[channel_name] if cb is not callback
                ]

    def dispatch(self, channel_name: str, msg: Any):
        """分发消息 — 零拷贝(直接传引用)"""
        with self._lock:
            listeners = list(self._listeners.get(channel_name, []))
        for listener in listeners:
            listener(msg)


class IntraTransmitter(Transmitter[T]):
    """
    进程内传输器 — 零拷贝

    CyberRT来源 (intra_transmitter.h):
      Transmit(msg, msg_info):
        IntraDispatcher::OnMessage(channel_id, msg, msg_info)
      零拷贝: 直接传shared_ptr，不序列化。

    Python适配: 直接传引用。
    """

    def __init__(self, channel_name: str):
        super().__init__(channel_name, TransportMode.INTRA)
        self._dispatcher = IntraDispatcher.instance()

    def enable(self):
        super().enable()

    def disable(self):
        super().disable()

    def transmit(self, msg: T) -> bool:
        if not self._enabled:
            return False
        t0 = time.monotonic_ns()
        self.next_seq_num()
        self._dispatcher.dispatch(self.channel_name, msg)
        self._transmit_count += 1
        self._total_latency_ns += time.monotonic_ns() - t0
        return True


class IntraReceiver(Receiver[T]):
    """进程内接收器"""

    def __init__(self, channel_name: str, callback: Callable[[T], None]):
        super().__init__(channel_name, callback)
        self._dispatcher = IntraDispatcher.instance()

    def enable(self):
        super().enable()
        self._dispatcher.add_listener(self.channel_name, self.on_new_message)

    def disable(self):
        super().disable()
        self._dispatcher.remove_listener(self.channel_name, self.on_new_message)


# ─── ShmTransmitter/Receiver ───

class ShmSegment:
    """
    共享内存段 — 对应 CyberRT transport::ShmConf + Segment

    CyberRT来源 (cyber/transport/shm/segment.h):
      Segment — 管理一块共享内存
      Block — 共享内存中的消息块
      State — 共享内存状态(可读/可写)

    PRD #27: memory_pool roadmap — typed allocators

    Python适配:
      使用 multiprocessing.shared_memory
      简化版: 固定大小的ring buffer布局

    延迟性能注意:
      CyberRT的SHM用posix_shm + 条件变量通知，p99 < 1ms。
      Python的shared_memory + polling，延迟会高于C++版本。
      对于MotionRequest(~80 bytes固定大小)，这是可接受的。
    """

    HEADER_SIZE = 64  # 头部: write_idx(8) + read_idx(8) + msg_size(8) + capacity(8) + ...
    MSG_SLOT_SIZE = 256  # 每个消息槽大小(对齐到256字节)

    def __init__(self, name: str, capacity: int = 64):
        self._name = name
        self._capacity = capacity
        total_size = self.HEADER_SIZE + capacity * self.MSG_SLOT_SIZE
        try:
            self._shm = shared_memory.SharedMemory(
                name=name, create=True, size=total_size
            )
            self._owner = True
            # 初始化头部
            struct.pack_into('QQQQ', self._shm.buf, 0, 0, 0, 0, capacity)
        except FileExistsError:
            self._shm = shared_memory.SharedMemory(name=name, create=False)
            self._owner = False

    def write(self, data: bytes) -> bool:
        """写入一条消息"""
        if len(data) > self.MSG_SLOT_SIZE - 8:  # 8字节长度头
            return False
        write_idx, read_idx, _, capacity = struct.unpack_from(
            'QQQQ', self._shm.buf, 0
        )
        next_idx = (write_idx + 1) % capacity
        if next_idx == read_idx:
            return False  # 满
        offset = self.HEADER_SIZE + int(write_idx) * self.MSG_SLOT_SIZE
        struct.pack_into(f'Q{len(data)}s', self._shm.buf, offset,
                         len(data), data)
        struct.pack_into('Q', self._shm.buf, 0, next_idx)
        return True

    def read(self) -> Optional[bytes]:
        """读取一条消息"""
        write_idx, read_idx, _, capacity = struct.unpack_from(
            'QQQQ', self._shm.buf, 0
        )
        if read_idx == write_idx:
            return None  # 空
        offset = self.HEADER_SIZE + int(read_idx) * self.MSG_SLOT_SIZE
        msg_len = struct.unpack_from('Q', self._shm.buf, offset)[0]
        data = bytes(self._shm.buf[offset + 8: offset + 8 + msg_len])
        next_read = (read_idx + 1) % capacity
        struct.pack_into('Q', self._shm.buf, 8, next_read)
        return data

    def close(self):
        self._shm.close()
        if self._owner:
            try:
                self._shm.unlink()
            except FileNotFoundError:
                pass

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


class ShmTransmitter(Transmitter[T]):
    """
    共享内存传输器

    CyberRT来源 (shm_transmitter.h):
      Transmit(msg, msg_info):
        1. 序列化 msg → bytes
        2. 获取 ShmBlock
        3. 拷贝到共享内存
        4. Notifier::Notify() — 唤醒reader

    延迟: CyberRT p99 < 1ms (posix_shm + futex通知)
    Python版: 预计 p99 < 5ms (shared_memory + polling)
    """

    def __init__(self, channel_name: str,
                 serializer: Callable[[T], bytes],
                 segment_capacity: int = 64):
        super().__init__(channel_name, TransportMode.SHM)
        self._serializer = serializer
        # 使用channel_name生成shm段名(sanitize)
        safe_name = channel_name.replace('/', '_').strip('_')[:30]
        self._segment = ShmSegment(
            name=f"world_{safe_name}",
            capacity=segment_capacity
        )

    def enable(self):
        super().enable()

    def disable(self):
        super().disable()
        self._segment.close()

    def transmit(self, msg: T) -> bool:
        if not self._enabled:
            return False
        t0 = time.monotonic_ns()
        self.next_seq_num()
        data = self._serializer(msg)
        ok = self._segment.write(data)
        self._transmit_count += 1
        self._total_latency_ns += time.monotonic_ns() - t0
        return ok


class ShmReceiver(Receiver[T]):
    """
    共享内存接收器 — polling模式

    CyberRT来源:
      ShmReceiver 通过 Notifier(条件变量) 被唤醒。
    Python适配:
      polling线程定期检查共享内存段。
    """

    def __init__(self, channel_name: str,
                 callback: Callable[[T], None],
                 deserializer: Callable[[bytes], T],
                 poll_interval_ms: float = 1.0):
        super().__init__(channel_name, callback)
        self._deserializer = deserializer
        self._poll_interval_s = poll_interval_ms / 1000.0
        safe_name = channel_name.replace('/', '_').strip('_')[:30]
        self._segment = ShmSegment(name=f"world_{safe_name}")
        self._poll_thread: Optional[threading.Thread] = None
        self._running = False

    def enable(self):
        super().enable()
        self._running = True
        self._poll_thread = threading.Thread(
            target=self._poll_loop, daemon=True
        )
        self._poll_thread.start()

    def disable(self):
        self._running = False
        super().disable()
        if self._poll_thread:
            self._poll_thread.join(timeout=1.0)
        self._segment.close()

    def _poll_loop(self):
        while self._running:
            data = self._segment.read()
            if data is not None:
                msg = self._deserializer(data)
                self.on_new_message(msg)
            else:
                time.sleep(self._poll_interval_s)


# ─── HybridTransmitter ───

class HybridTransmitter(Transmitter[T]):
    """
    混合传输器 — 按拓扑距离自动选择传输模式

    CyberRT来源 (hybrid_transmitter.h):
      内部持有 IntraTransmitter + ShmTransmitter + RtpsTransmitter
      Enable(opposite_attr) 时根据对端的进程/机器拓扑选择:
        同进程 → IntraTransmitter (零拷贝)
        同机器不同进程 → ShmTransmitter (共享内存)
        不同机器 → RtpsTransmitter (DDS网络)

    PRD #17: 按拓扑距离自动路由

    当前实现:
      只有 Intra 和 SHM 两种模式。
      同进程默认用Intra，显式指定SHM时用SHM。
      RTPS(跨节点)暂不实现 — pubsub-loop当前是单机多进程场景。

    还不知道的事:
      RTPS/SHM在"个体运动申请→世界确认"pattern下延迟表现
      → 先用Intra(零拷贝)做默认路径，SHM做多进程路径。
        不要过早优化transport层。
    """

    def __init__(self, channel_name: str,
                 serializer: Optional[Callable[[T], bytes]] = None,
                 deserializer: Optional[Callable[[bytes], T]] = None):
        super().__init__(channel_name, TransportMode.HYBRID)
        self._intra = IntraTransmitter(channel_name)
        self._shm: Optional[ShmTransmitter] = None
        self._serializer = serializer
        if serializer:
            self._shm = ShmTransmitter(channel_name, serializer)

    def enable(self):
        super().enable()
        self._intra.enable()
        if self._shm:
            self._shm.enable()

    def disable(self):
        super().disable()
        self._intra.disable()
        if self._shm:
            self._shm.disable()

    def transmit(self, msg: T) -> bool:
        if not self._enabled:
            return False
        t0 = time.monotonic_ns()
        # 同进程内: 直接用intra(零拷贝，最快)
        ok = self._intra.transmit(msg)
        # 如果有shm: 同时写入shm(供其他进程读取)
        if self._shm:
            self._shm.transmit(msg)
        self._transmit_count += 1
        self._total_latency_ns += time.monotonic_ns() - t0
        return ok


# ─── Transport 工厂 ───

class Transport:
    """
    传输层工厂 — 对应 CyberRT transport::Transport

    CyberRT来源 (transport.h):
      CreateTransmitter<M>(attr, mode) → Transmitter*
      CreateReceiver<M>(attr, callback, mode) → Receiver*

    使用:
      transport = Transport.instance()
      tx = transport.create_transmitter(channel, mode=TransportMode.HYBRID)
      rx = transport.create_receiver(channel, callback, mode=TransportMode.INTRA)
    """
    _instance = None
    _lock = threading.Lock()

    @classmethod
    def instance(cls) -> 'Transport':
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = cls()
        return cls._instance

    def __init__(self):
        self._transmitters: Dict[str, Transmitter] = {}
        self._receivers: Dict[str, List[Receiver]] = {}

    def create_transmitter(self, channel_name: str,
                           mode: TransportMode = TransportMode.INTRA,
                           serializer: Optional[Callable] = None,
                           ) -> Transmitter:
        """创建传输器 — CyberRT: Transport::CreateTransmitter"""
        if mode == TransportMode.INTRA:
            tx = IntraTransmitter(channel_name)
        elif mode == TransportMode.SHM:
            if serializer is None:
                raise ValueError("SHM mode requires serializer")
            tx = ShmTransmitter(channel_name, serializer)
        elif mode == TransportMode.HYBRID:
            tx = HybridTransmitter(channel_name, serializer)
        else:
            tx = IntraTransmitter(channel_name)

        tx.enable()
        self._transmitters[channel_name] = tx
        return tx

    def create_receiver(self, channel_name: str,
                        callback: Callable,
                        mode: TransportMode = TransportMode.INTRA,
                        deserializer: Optional[Callable] = None,
                        ) -> Receiver:
        """创建接收器 — CyberRT: Transport::CreateReceiver"""
        if mode == TransportMode.INTRA:
            rx = IntraReceiver(channel_name, callback)
        elif mode == TransportMode.SHM:
            if deserializer is None:
                raise ValueError("SHM mode requires deserializer")
            rx = ShmReceiver(channel_name, callback, deserializer)
        else:
            rx = IntraReceiver(channel_name, callback)

        rx.enable()
        if channel_name not in self._receivers:
            self._receivers[channel_name] = []
        self._receivers[channel_name].append(rx)
        return rx

    def shutdown(self):
        for tx in self._transmitters.values():
            tx.disable()
        for rxs in self._receivers.values():
            for rx in rxs:
                rx.disable()
