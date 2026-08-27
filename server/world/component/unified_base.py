"""
world.component.unified_base — unified component base with typed pub/sub
=========================================================================

PRD #98: Refactor individual component base interface to unify state
publishing and subscription entry points.

Problem: Components currently include separate publisher_base/subscriber_base
with distinct APIs for publishing and subscribing state.  This causes:
1. Boilerplate: every component imports two bases and calls two sets of APIs
2. No lifecycle enforcement: framework can't verify symmetric channel ownership
3. No compile-time type safety between publish and subscribe sides

Solution: Single ComponentBase CRTP base with unified Publish<T>/Subscribe<T>
entry points, enforced lifecycle (init → ready → active → shutdown), and
type-checked channel handles.

Sources:
  - CyberRT: component/component_base.h
  - agent-governance-toolkit: agent lifecycle management
"""

from __future__ import annotations

import threading
from abc import ABC, abstractmethod
from enum import Enum
from typing import (
    Any, Callable, Dict, Generic, List, Optional, Set, Type, TypeVar,
)

T = TypeVar('T')


class ComponentLifecycle(Enum):
    """Component lifecycle states."""
    CREATED = 'created'
    INITIALIZING = 'initializing'
    READY = 'ready'
    ACTIVE = 'active'
    PAUSED = 'paused'
    SHUTTING_DOWN = 'shutting_down'
    SHUTDOWN = 'shutdown'


class ChannelHandle(Generic[T]):
    """Type-safe handle to a pub/sub channel.

    Created by ComponentBase.publish() / ComponentBase.subscribe().
    Carries the message type for compile-time-like checking.
    """

    __slots__ = ('_name', '_msg_type', '_direction', '_callback', '_active')

    def __init__(
        self,
        name: str,
        msg_type: Type[T],
        direction: str,
        callback: Optional[Callable[[T], None]] = None,
    ):
        self._name = name
        self._msg_type = msg_type
        self._direction = direction  # 'pub' or 'sub'
        self._callback = callback
        self._active = True

    @property
    def channel_name(self) -> str:
        return self._name

    @property
    def msg_type(self) -> Type[T]:
        return self._msg_type

    @property
    def is_publisher(self) -> bool:
        return self._direction == 'pub'

    @property
    def is_subscriber(self) -> bool:
        return self._direction == 'sub'

    @property
    def is_active(self) -> bool:
        return self._active

    def deactivate(self):
        self._active = False


class UnifiedComponentBase(ABC):
    """Unified component base with symmetric Publish<T>/Subscribe<T>.

    Usage::

        class SensorComponent(UnifiedComponentBase):
            def on_init(self) -> bool:
                self.lidar_pub = self.publish('lidar/state', LidarState)
                self.cmd_sub = self.subscribe('cmd/velocity', VelocityCmd, self.on_cmd)
                return True

            def on_cmd(self, cmd: VelocityCmd):
                ...

            def on_tick(self, tick_num: int):
                state = self.compute_state()
                self.write(self.lidar_pub, state)  # type-checked
    """

    def __init__(self, name: str, individual_id: int = 0):
        self._name = name
        self._individual_id = individual_id
        self._lifecycle = ComponentLifecycle.CREATED
        self._pub_handles: Dict[str, ChannelHandle] = {}
        self._sub_handles: Dict[str, ChannelHandle] = {}
        self._write_callbacks: Dict[str, Callable] = {}
        self._lock = threading.Lock()

    # -- Lifecycle -----------------------------------------------------------

    def initialize(self) -> bool:
        """Framework calls this to initialize the component."""
        self._lifecycle = ComponentLifecycle.INITIALIZING
        ok = self.on_init()
        if ok:
            self._lifecycle = ComponentLifecycle.READY
        return ok

    def activate(self):
        """Transition from READY to ACTIVE."""
        if self._lifecycle != ComponentLifecycle.READY:
            raise RuntimeError(
                f"Cannot activate from {self._lifecycle.value}; must be READY"
            )
        self._lifecycle = ComponentLifecycle.ACTIVE

    def shutdown(self):
        """Shutdown the component, deactivating all channels."""
        self._lifecycle = ComponentLifecycle.SHUTTING_DOWN
        for h in list(self._pub_handles.values()) + list(self._sub_handles.values()):
            h.deactivate()
        self.on_shutdown()
        self._lifecycle = ComponentLifecycle.SHUTDOWN

    @property
    def lifecycle(self) -> ComponentLifecycle:
        return self._lifecycle

    @property
    def name(self) -> str:
        return self._name

    @property
    def individual_id(self) -> int:
        return self._individual_id

    # -- Typed channel API ---------------------------------------------------

    def publish(self, channel_name: str, msg_type: Type[T]) -> ChannelHandle[T]:
        """Declare a publish channel.

        Returns a typed handle for use with write().
        Type mismatches are caught at write() time.
        """
        handle = ChannelHandle(channel_name, msg_type, 'pub')
        self._pub_handles[channel_name] = handle
        return handle

    def subscribe(
        self,
        channel_name: str,
        msg_type: Type[T],
        callback: Callable[[T], None],
    ) -> ChannelHandle[T]:
        """Declare a subscribe channel with typed callback.

        The callback receives messages of exactly msg_type.
        """
        handle = ChannelHandle(channel_name, msg_type, 'sub', callback)
        self._sub_handles[channel_name] = handle
        return handle

    def write(self, handle: ChannelHandle[T], msg: T) -> bool:
        """Write a message through a publish handle.

        Type-checks msg against the handle's declared msg_type.
        """
        if not handle.is_active:
            return False
        if not handle.is_publisher:
            raise TypeError(f"Cannot write to subscriber handle '{handle.channel_name}'")
        if not isinstance(msg, handle.msg_type):
            raise TypeError(
                f"Channel '{handle.channel_name}' expects {handle.msg_type.__qualname__}, "
                f"got {type(msg).__qualname__}"
            )
        # Deliver to write callbacks (connected by framework)
        cb = self._write_callbacks.get(handle.channel_name)
        if cb:
            cb(msg)
        return True

    def deliver(self, channel_name: str, msg: Any):
        """Framework calls this to deliver a message to a subscriber."""
        handle = self._sub_handles.get(channel_name)
        if handle and handle.is_active and handle._callback:
            if isinstance(msg, handle.msg_type):
                handle._callback(msg)

    def connect_write(self, channel_name: str, callback: Callable):
        """Framework connects a write callback for a publish channel."""
        self._write_callbacks[channel_name] = callback

    # -- Channel introspection -----------------------------------------------

    def published_channels(self) -> Dict[str, Type]:
        """Return {channel_name: msg_type} for all publish channels."""
        return {name: h.msg_type for name, h in self._pub_handles.items()}

    def subscribed_channels(self) -> Dict[str, Type]:
        """Return {channel_name: msg_type} for all subscribe channels."""
        return {name: h.msg_type for name, h in self._sub_handles.items()}

    def verify_symmetric_ownership(self) -> List[str]:
        """Check for channels that are both published and subscribed (potential loop)."""
        overlap = set(self._pub_handles) & set(self._sub_handles)
        return [f"Channel '{ch}' is both published and subscribed" for ch in overlap]

    # -- Abstract hooks (subclass implements) --------------------------------

    @abstractmethod
    def on_init(self) -> bool:
        """Initialize the component: declare channels here."""
        ...

    def on_tick(self, tick_num: int):
        """Called each tick. Override to publish state."""
        pass

    def on_shutdown(self):
        """Called on shutdown. Override to clean up."""
        pass
