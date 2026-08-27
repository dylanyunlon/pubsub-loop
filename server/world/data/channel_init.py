"""
world.data.channel_init — auto-zero-init channel buffer
=========================================================

PRD #39: data::ChannelBuffer auto-zero-init — initialize channel content
with state type default values at subscription time, eliminating undefined
read risk.

Problem: When an individual subscribes to a channel before any publisher has
written, CacheBuffer.front() returns None.  Downstream consumers crash on
None.value_or() chains or produce NaN-poisoned state.

Solution: ZeroInitChannelBuffer wraps CacheBuffer, filling all ring slots with
T() (default-constructed value) at construction time.  latest() always returns
a valid value, never None.

Sources:
  - CyberRT: data/channel_buffer auto-init
  - pubsub-loop: data/data_core.py CacheBuffer
"""

from __future__ import annotations

from typing import Callable, Generic, List, Optional, Type, TypeVar

from world.data.data_core import CacheBuffer

T = TypeVar('T')


class ZeroInitChannelBuffer(CacheBuffer[T]):
    """CacheBuffer with auto-zero-initialization at construction.

    All ring buffer slots are filled with default_value at construction time.
    latest() and front() always return a valid T, never None.

    Usage::

        @dataclass
        class IndividualPos:
            x: float = 0.0
            y: float = 0.0
            z: float = 0.0

        buf = ZeroInitChannelBuffer(IndividualPos, capacity=64)
        pos = buf.front()  # always returns IndividualPos, never None
    """

    def __init__(
        self,
        msg_type: Type[T],
        capacity: int = 64,
        default_value: Optional[T] = None,
    ):
        super().__init__(capacity)
        self._msg_type = msg_type
        self._has_published = False

        # Compute default value
        if default_value is not None:
            self._default = default_value
        else:
            try:
                self._default = msg_type()
            except TypeError:
                raise TypeError(
                    f"{msg_type.__qualname__} is not default-constructible. "
                    f"Pass an explicit default_value to ZeroInitChannelBuffer."
                )

        # Fill all slots with default
        for _ in range(capacity):
            super().fill(self._default)

    @property
    def msg_type(self) -> Type[T]:
        return self._msg_type

    @property
    def has_published(self) -> bool:
        """Whether any real data has been published (vs default values)."""
        return self._has_published

    @property
    def default_value(self) -> T:
        return self._default

    def fill(self, msg: T) -> bool:
        """Write a message, marking the buffer as having real data."""
        if not isinstance(msg, self._msg_type):
            raise TypeError(
                f"Expected {self._msg_type.__qualname__}, "
                f"got {type(msg).__qualname__}"
            )
        self._has_published = True
        return super().fill(msg)

    def front(self) -> T:
        """Return oldest message — guaranteed non-None."""
        result = super().front()
        return result if result is not None else self._default

    def back(self) -> T:
        """Return newest message — guaranteed non-None."""
        result = super().back()
        return result if result is not None else self._default

    def latest(self, n: int = 1) -> List[T]:
        """Return latest n messages — guaranteed all non-None."""
        results = super().latest(n)
        if not results:
            return [self._default]
        return results

    def reset_to_default(self):
        """Reset all slots to default value."""
        self._has_published = False
        for _ in range(self.capacity):
            super().fill(self._default)
