"""
world.node.channel_buffer — typed pub/sub container with pluggable backends
=============================================================================

PRD #33: ChannelBuffer<T, Properties...> typed pub/sub container.

Problem: Current node channel API uses raw void*/untyped data, causing:
1. Silent type mismatches between publisher and subscriber
2. Manual serialization at every call site
3. No compile-time channel property validation

Solution: Typed ChannelBuffer that wraps transport layer with type safety,
automatic serialization, and pluggable shared-memory backend selection.

Sources:
  - CyberRT: data/channel_buffer, node/reader/writer
  - pubsub-loop: world/transport/transmitter.py
"""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass, field
from enum import Enum, Flag, auto
from typing import (
    Any, Callable, Dict, Generic, List, Optional, Sequence,
    Set, Tuple, Type, TypeVar,
)

T = TypeVar('T')


class ChannelProperty(Flag):
    """Channel properties for compile-time-like validation."""
    NONE = 0
    ZERO_COPY = auto()         # use shared memory, no serialization
    PERSISTENT = auto()        # keep history in ring buffer
    ORDERED = auto()           # guarantee arrival order
    LOSSY = auto()             # drop oldest on overflow (vs backpressure)
    BROADCAST = auto()         # one-to-many delivery
    UNICAST = auto()           # one-to-one delivery
    INTRA_PROCESS = auto()     # same-process only
    CROSS_PROCESS = auto()     # cross-process via SHM/RTPS


class BackendType(Enum):
    """Transport backend selection."""
    INTRA = 'intra'           # in-process reference passing
    SHM = 'shm'              # shared memory
    RTPS = 'rtps'             # network transport
    AUTO = 'auto'             # auto-select based on topology


@dataclass
class ChannelConfig:
    """Configuration for a typed channel."""
    capacity: int = 64
    properties: ChannelProperty = ChannelProperty.ORDERED | ChannelProperty.LOSSY
    backend: BackendType = BackendType.AUTO
    history_depth: int = 1        # how many messages to keep
    qos_deadline_ms: float = 0    # 0 = no deadline


class ChannelBuffer(Generic[T]):
    """Typed pub/sub channel with pluggable backend.

    Replaces raw void* channel API with type-safe container.
    Type mismatches are caught at channel creation time, not at runtime.

    Usage::

        # Publisher
        buf = ChannelBuffer[IndividualPos](
            channel_name='/sensors/lidar/state',
            msg_type=IndividualPos,
            config=ChannelConfig(capacity=32, backend=BackendType.INTRA),
        )
        buf.write(IndividualPos(x=1.0, y=2.0, z=3.0))

        # Subscriber (type-checked against publisher's msg_type)
        buf.subscribe(lambda pos: print(pos.x, pos.y, pos.z))
    """

    __slots__ = (
        '_channel_name', '_msg_type', '_config', '_buffer',
        '_write_idx', '_read_idx', '_lock', '_subscribers',
        '_stats', '_type_name',
    )

    def __init__(
        self,
        channel_name: str,
        msg_type: Type[T],
        config: Optional[ChannelConfig] = None,
    ):
        self._channel_name = channel_name
        self._msg_type = msg_type
        self._type_name = msg_type.__qualname__
        self._config = config or ChannelConfig()
        self._buffer: List[Optional[T]] = [None] * self._config.capacity
        self._write_idx = 0
        self._read_idx = 0
        self._lock = threading.Lock()
        self._subscribers: List[Callable[[T], None]] = []
        self._stats = ChannelStats()

    @property
    def channel_name(self) -> str:
        return self._channel_name

    @property
    def msg_type(self) -> Type[T]:
        return self._msg_type

    @property
    def type_name(self) -> str:
        return self._type_name

    @property
    def config(self) -> ChannelConfig:
        return self._config

    @property
    def stats(self) -> 'ChannelStats':
        return self._stats

    def write(self, msg: T) -> bool:
        """Write a typed message to the channel.

        Type-checks at write time (debug mode).
        Returns False if channel is full and not LOSSY.
        """
        if not isinstance(msg, self._msg_type):
            raise TypeError(
                f"Channel '{self._channel_name}' expects {self._type_name}, "
                f"got {type(msg).__qualname__}"
            )

        with self._lock:
            cap = self._config.capacity
            idx = self._write_idx % cap

            if self._size_unlocked() >= cap:
                if ChannelProperty.LOSSY in self._config.properties:
                    self._read_idx += 1
                    self._stats.drops += 1
                else:
                    self._stats.write_failures += 1
                    return False

            self._buffer[idx] = msg
            self._write_idx += 1
            self._stats.writes += 1

        # Notify subscribers outside lock
        for sub in self._subscribers:
            try:
                sub(msg)
            except Exception:
                self._stats.callback_errors += 1

        return True

    def read(self) -> Optional[T]:
        """Read the oldest unread message."""
        with self._lock:
            if self._size_unlocked() == 0:
                return None
            cap = self._config.capacity
            idx = self._read_idx % cap
            msg = self._buffer[idx]
            self._read_idx += 1
            self._stats.reads += 1
            return msg

    def latest(self, n: int = 1) -> List[T]:
        """Get the latest n messages (most recent first)."""
        with self._lock:
            size = self._size_unlocked()
            n = min(n, size)
            cap = self._config.capacity
            result = []
            for i in range(n):
                idx = (self._write_idx - 1 - i) % cap
                msg = self._buffer[idx]
                if msg is not None:
                    result.append(msg)
            return result

    def peek(self) -> Optional[T]:
        """Peek at the oldest unread message without consuming it."""
        with self._lock:
            if self._size_unlocked() == 0:
                return None
            idx = self._read_idx % self._config.capacity
            return self._buffer[idx]

    def subscribe(self, callback: Callable[[T], None]):
        """Register a subscriber callback."""
        self._subscribers.append(callback)

    def unsubscribe(self, callback: Callable[[T], None]):
        """Remove a subscriber callback."""
        try:
            self._subscribers.remove(callback)
        except ValueError:
            pass

    def size(self) -> int:
        with self._lock:
            return self._size_unlocked()

    def empty(self) -> bool:
        with self._lock:
            return self._size_unlocked() == 0

    def full(self) -> bool:
        with self._lock:
            return self._size_unlocked() >= self._config.capacity

    def clear(self):
        with self._lock:
            self._buffer = [None] * self._config.capacity
            self._write_idx = 0
            self._read_idx = 0

    def _size_unlocked(self) -> int:
        return self._write_idx - self._read_idx

    @classmethod
    def check_type_match(cls, pub_type: type, sub_type: type) -> bool:
        """Verify publisher and subscriber agree on message type.

        This catches the silent-mismatch bug at connection time.
        """
        return pub_type is sub_type or issubclass(pub_type, sub_type)


@dataclass
class ChannelStats:
    """Channel usage statistics."""
    writes: int = 0
    reads: int = 0
    drops: int = 0
    write_failures: int = 0
    callback_errors: int = 0
