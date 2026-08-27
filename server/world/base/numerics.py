# SPDX-License-Identifier: Apache-2.0
#
# world/base/numerics.py
#
# 数值类型与位操作原语 — PRD #29, #19, #90
#
# CyberRT来源:
#   cyber/base/bounded_queue.h 中的 CAS 原子操作语义
#   跨节点传输时的字节序处理(transport层隐含需求)
#
# governance来源:
#   trust_score 的定点数表示(避免浮点比较不确定性)
#
# 解决的问题:
#   PRD #29: FixedPoint — 跨节点(x86/ARM/RTOS)状态传输时
#            浮点表示不一致。MotionRequest 的 delta_position 用
#            FixedPoint<int32, -16> 替代 float，保证二进制一致。
#   PRD #19: 对齐工具(已在primitives.py，此处扩展)
#   PRD #90: find_first_set — 原子个体状态标志位扫描

import struct
import sys
import threading
from typing import TypeVar, Generic, Optional

T = TypeVar('T')

# ─── Byte-order utilities ───
# CyberRT transport 需要在 x86(little-endian) 和 ARM(可配置) 之间传输。
# pubsub-loop 的 MotionRequest 在 SHM transport 中用 struct.pack,
# 需要统一字节序。

NATIVE_BYTE_ORDER = sys.byteorder  # 'little' or 'big'


def byte_swap_16(value: int) -> int:
    """16位字节序翻转"""
    return ((value & 0xFF) << 8) | ((value >> 8) & 0xFF)


def byte_swap_32(value: int) -> int:
    """32位字节序翻转 — 用于FixedPoint<int32>的跨节点传输"""
    return (
        ((value & 0x000000FF) << 24) |
        ((value & 0x0000FF00) << 8)  |
        ((value & 0x00FF0000) >> 8)  |
        ((value >> 24) & 0xFF)
    )


def byte_swap_64(value: int) -> int:
    """64位字节序翻转 — 用于tick_seq等uint64字段"""
    b = value.to_bytes(8, byteorder='little')
    return int.from_bytes(b, byteorder='big')


def to_network_order_32(value: int) -> int:
    """转为网络字节序(big-endian) — RTPS传输需要"""
    if NATIVE_BYTE_ORDER == 'big':
        return value
    return byte_swap_32(value)


def from_network_order_32(value: int) -> int:
    """从网络字节序转回本地序"""
    if NATIVE_BYTE_ORDER == 'big':
        return value
    return byte_swap_32(value)


# ─── FixedPoint — PRD #29 ───
#
# 问题: float在x86和ARM上的IEEE 754虽然相同，但：
#   1. 不同编译器的浮点舍入模式可能不同
#   2. denormalized number处理不一致(ARM某些模式flush to zero)
#   3. NaN payload不一致
#
# 解决: MotionRequest.delta_position用FixedPoint<int32, 16>
#   分辨率: 2^-16 ≈ 0.0000153 (15微米，足够精确)
#   范围: [-32768, 32767] (足够覆盖世界坐标)
#   二进制确定性: 整数运算，跨平台完全一致
#
# CyberRT没有FixedPoint，这是pubsub-loop对跨平台需求的扩展。
# governance的trust_score也可以用FixedPoint表示，避免浮点比较。

class FixedPoint:
    """
    定点数 — 整数表示的小数

    FixedPoint(raw, frac_bits) where value = raw / 2^frac_bits

    PRD #29: 跨节点消息安全的数值类型
    替代float用于MotionRequest的position/velocity字段。

    用法:
        # 创建: 从浮点数
        fp = FixedPoint.from_float(3.14, frac_bits=16)

        # 运算: 加减乘(保持定点)
        result = fp + FixedPoint.from_float(1.0, frac_bits=16)

        # 传输: 序列化为4字节整数
        raw = fp.raw  # int32, 可直接struct.pack('i', raw)

        # 接收端: 反序列化
        fp2 = FixedPoint(raw, frac_bits=16)
        value = fp2.to_float()  # 3.14 (精确到2^-16)
    """
    __slots__ = ('_raw', '_frac_bits', '_scale')

    def __init__(self, raw: int = 0, frac_bits: int = 16):
        self._raw = raw
        self._frac_bits = frac_bits
        self._scale = 1 << frac_bits

    @classmethod
    def from_float(cls, value: float, frac_bits: int = 16) -> 'FixedPoint':
        """从浮点数创建 — 用于发送端"""
        scale = 1 << frac_bits
        raw = int(round(value * scale))
        # 钳位到int32范围
        raw = max(-2147483648, min(2147483647, raw))
        return cls(raw, frac_bits)

    def to_float(self) -> float:
        """转为浮点数 — 用于接收端"""
        return self._raw / self._scale

    @property
    def raw(self) -> int:
        """原始整数值 — 用于序列化传输"""
        return self._raw

    @property
    def frac_bits(self) -> int:
        return self._frac_bits

    def __add__(self, other: 'FixedPoint') -> 'FixedPoint':
        assert self._frac_bits == other._frac_bits
        return FixedPoint(self._raw + other._raw, self._frac_bits)

    def __sub__(self, other: 'FixedPoint') -> 'FixedPoint':
        assert self._frac_bits == other._frac_bits
        return FixedPoint(self._raw - other._raw, self._frac_bits)

    def __mul__(self, other: 'FixedPoint') -> 'FixedPoint':
        """定点乘法 — 乘完右移frac_bits位"""
        assert self._frac_bits == other._frac_bits
        product = self._raw * other._raw
        result = product >> self._frac_bits
        return FixedPoint(result, self._frac_bits)

    def __neg__(self) -> 'FixedPoint':
        return FixedPoint(-self._raw, self._frac_bits)

    def __abs__(self) -> 'FixedPoint':
        return FixedPoint(abs(self._raw), self._frac_bits)

    def __lt__(self, other: 'FixedPoint') -> bool:
        assert self._frac_bits == other._frac_bits
        return self._raw < other._raw

    def __le__(self, other: 'FixedPoint') -> bool:
        assert self._frac_bits == other._frac_bits
        return self._raw <= other._raw

    def __eq__(self, other) -> bool:
        if not isinstance(other, FixedPoint):
            return NotImplemented
        return self._raw == other._raw and self._frac_bits == other._frac_bits

    def __repr__(self):
        return f"FixedPoint({self.to_float():.6f}, frac={self._frac_bits})"

    def pack(self) -> bytes:
        """序列化为4字节 — SHM/RTPS transport 传输"""
        return struct.pack('<i', self._raw)

    @classmethod
    def unpack(cls, data: bytes, frac_bits: int = 16) -> 'FixedPoint':
        """反序列化"""
        raw = struct.unpack('<i', data[:4])[0]
        return cls(raw, frac_bits)


def clamp(value, lo, hi):
    """数值钳位 — PRD #123: base::min/max/clamp"""
    if value < lo:
        return lo
    if value > hi:
        return hi
    return value


def lerp(a: float, b: float, t: float) -> float:
    """线性插值 — 用于渲染层状态平滑"""
    return a + (b - a) * t


# ─── Bit-scan: find_first_set — PRD #90 ───
#
# 用途: 个体状态标志位扫描
# CollisionLayer 是位掩码(IntFlag), 需要快速找到第一个设置的位。
# 例: layer = STATIC | TRIGGER = 0b1010
#      ffs(layer) → 1 (第1位, TRIGGER)
#
# CyberRT: 没有直接对应, 但 scheduler 的优先级查找用了类似操作。
# Apollo用GCC __builtin_ffs, Python用 bit_length() 模拟。

def find_first_set(value: int) -> int:
    """
    查找最低设置位的位置 (1-indexed, 0表示无设置位)

    PRD #90: 原子个体状态标志位扫描

    >>> find_first_set(0b1010)
    2
    >>> find_first_set(0b1000)
    4
    >>> find_first_set(0)
    0
    """
    if value == 0:
        return 0
    # 隔离最低设置位: value & (-value) = lowest set bit
    lowest = value & (-value)
    return lowest.bit_length()


def popcount(value: int) -> int:
    """
    设置位计数 — 统计CollisionLayer中有多少个图层

    >>> popcount(0b1010)
    2
    """
    return bin(value).count('1')


def next_power_of_two(value: int) -> int:
    """
    向上取最近的2的幂 — 用于 BoundedQueue 容量对齐

    >>> next_power_of_two(5)
    8
    >>> next_power_of_two(8)
    8
    """
    if value <= 1:
        return 1
    return 1 << (value - 1).bit_length()


# ─── AtomicCounter — PRD #30 ───
#
# CyberRT: std::atomic<T> 全局使用
# Python: GIL保证了单个字节码操作的原子性,
# 但复合操作(如 fetch_add)仍需锁。
#
# 用途:
#   - Transport.next_seq_num() — 消息序列号递增
#   - GlobalData.RegisterTaskName() — 任务ID分配
#   - WorldResolver — 碰撞事件计数

class AtomicCounter:
    """
    原子计数器 — 线程安全的递增/递减

    CyberRT: std::atomic<uint64_t>
    Python: Lock + int (GIL不保证复合操作原子性)
    """
    __slots__ = ('_value', '_lock')

    def __init__(self, initial: int = 0):
        self._value = initial
        self._lock = threading.Lock()

    def fetch_add(self, delta: int = 1) -> int:
        """原子加 — 返回旧值"""
        with self._lock:
            old = self._value
            self._value += delta
            return old

    def fetch_sub(self, delta: int = 1) -> int:
        """原子减 — 返回旧值"""
        with self._lock:
            old = self._value
            self._value -= delta
            return old

    def load(self) -> int:
        """原子读"""
        with self._lock:
            return self._value

    def store(self, value: int):
        """原子写"""
        with self._lock:
            self._value = value

    def compare_exchange(self, expected: int, desired: int) -> bool:
        """CAS — 如果当前值等于expected，设为desired"""
        with self._lock:
            if self._value == expected:
                self._value = desired
                return True
            return False


class AtomicFlag:
    """
    原子标志位 — 用于个体状态位掩码

    支持原子 set_bit / clear_bit / test_bit 操作。
    用于 CollisionLayer 的并发更新场景。
    """
    __slots__ = ('_bits', '_lock')

    def __init__(self, initial: int = 0):
        self._bits = initial
        self._lock = threading.Lock()

    def set_bit(self, position: int):
        """设置指定位"""
        with self._lock:
            self._bits |= (1 << position)

    def clear_bit(self, position: int):
        """清除指定位"""
        with self._lock:
            self._bits &= ~(1 << position)

    def test_bit(self, position: int) -> bool:
        """测试指定位"""
        with self._lock:
            return bool(self._bits & (1 << position))

    def fetch_or(self, mask: int) -> int:
        """原子OR — 返回旧值"""
        with self._lock:
            old = self._bits
            self._bits |= mask
            return old

    def fetch_and(self, mask: int) -> int:
        """原子AND — 返回旧值"""
        with self._lock:
            old = self._bits
            self._bits &= mask
            return old

    def load(self) -> int:
        with self._lock:
            return self._bits

    def first_set(self) -> int:
        """查找最低设置位 — 复合操作"""
        with self._lock:
            return find_first_set(self._bits)
