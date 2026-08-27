"""
world.data.stream_task_flow — platform-compatible streaming task pipeline
==========================================================================

PRD #47: Fix data::StreamTaskFlow MSVC compilation — eliminate MSVC-specific
C++ syntax incompatibilities (structured binding lambda capture, __attribute__).

In Python, the MSVC issues don't apply, but the StreamTaskFlow pattern
(pub/sub pipeline with task chaining) is useful infrastructure.

Sources:
  - CyberRT: data/stream_task_flow
  - taskflow: C++ parallel task library
"""

from __future__ import annotations

import threading
from collections import deque
from typing import Any, Callable, Dict, Generic, List, Optional, Tuple, TypeVar

T = TypeVar('T')
U = TypeVar('U')


class PubSubPair(Generic[T]):
    """Named publisher/subscriber pair (avoids structured binding issues).

    In C++, the MSVC fix was to replace:
        auto [pub, sub] = create_channel<T>(id);
    with:
        auto channel = create_channel<T>(id);
        auto pub = std::move(channel.publisher);

    Python equivalent: explicit named attributes.
    """

    __slots__ = ('publisher', 'subscriber', 'channel_id')

    def __init__(self, channel_id: str):
        self.channel_id = channel_id
        self._queue: deque = deque()
        self.publisher = Publisher(self._queue, channel_id)
        self.subscriber = Subscriber(self._queue, channel_id)


class Publisher(Generic[T]):
    """Publisher end of a PubSubPair."""
    __slots__ = ('_queue', '_channel_id', '_send_count')

    def __init__(self, queue: deque, channel_id: str):
        self._queue = queue
        self._channel_id = channel_id
        self._send_count = 0

    def send(self, state: T):
        self._queue.append(state)
        self._send_count += 1

    @property
    def send_count(self) -> int:
        return self._send_count


class Subscriber(Generic[T]):
    """Subscriber end of a PubSubPair."""
    __slots__ = ('_queue', '_channel_id')

    def __init__(self, queue: deque, channel_id: str):
        self._queue = queue
        self._channel_id = channel_id

    def receive(self) -> Optional[T]:
        try:
            return self._queue.popleft()
        except IndexError:
            return None

    def try_receive(self) -> Tuple[bool, Optional[T]]:
        try:
            return (True, self._queue.popleft())
        except IndexError:
            return (False, None)

    @property
    def pending(self) -> int:
        return len(self._queue)


class StreamTask:
    """A single task in the streaming pipeline."""

    __slots__ = ('_name', '_func', '_dependencies', '_is_complete')

    def __init__(self, name: str, func: Callable[[], None]):
        self._name = name
        self._func = func
        self._dependencies: List['StreamTask'] = []
        self._is_complete = False

    def depends_on(self, *tasks: 'StreamTask') -> 'StreamTask':
        self._dependencies.extend(tasks)
        return self

    def execute(self):
        """Execute this task (after dependencies are satisfied)."""
        if not all(d._is_complete for d in self._dependencies):
            raise RuntimeError(
                f"Task '{self._name}': not all dependencies are complete"
            )
        self._func()
        self._is_complete = True

    @property
    def name(self) -> str:
        return self._name

    @property
    def is_complete(self) -> bool:
        return self._is_complete

    def reset(self):
        self._is_complete = False


class StreamTaskFlow:
    """Streaming task pipeline with typed pub/sub channels.

    Replaces the MSVC-incompatible C++ version with a platform-agnostic
    Python implementation.  Tasks are connected via PubSubPairs.

    Usage::

        flow = StreamTaskFlow("sensor_pipeline")
        channel = flow.create_channel("lidar_data")

        pub_task = flow.add_task("publish", lambda: channel.publisher.send(data))
        proc_task = flow.add_task("process", lambda: process(channel.subscriber.receive()))
        proc_task.depends_on(pub_task)

        flow.run()  # executes in dependency order
    """

    def __init__(self, name: str = 'default'):
        self._name = name
        self._tasks: List[StreamTask] = []
        self._channels: Dict[str, PubSubPair] = {}
        self._stats = FlowStats()

    def create_channel(self, channel_id: str) -> PubSubPair:
        """Create a typed pub/sub channel pair.

        Returns named pair (no structured bindings needed).
        """
        pair = PubSubPair(channel_id)
        self._channels[channel_id] = pair
        return pair

    def add_task(self, name: str, func: Callable[[], None]) -> StreamTask:
        """Add a task to the pipeline."""
        task = StreamTask(name, func)
        self._tasks.append(task)
        return task

    def make_pub_task(
        self, channel_id: str, producer: Callable[[], Any],
    ) -> StreamTask:
        """Create a publishing task.

        MSVC-safe: uses explicit named pair, not structured binding.
        """
        pair = self._channels.get(channel_id)
        if pair is None:
            pair = self.create_channel(channel_id)

        # Capture explicit publisher reference (MSVC fix)
        pub = pair.publisher

        def pub_func():
            state = producer()
            pub.send(state)

        return self.add_task(f"pub_{channel_id}", pub_func)

    def make_sub_task(
        self, channel_id: str, consumer: Callable[[Any], None],
    ) -> StreamTask:
        """Create a subscribing task."""
        pair = self._channels.get(channel_id)
        if pair is None:
            pair = self.create_channel(channel_id)

        sub = pair.subscriber

        def sub_func():
            msg = sub.receive()
            if msg is not None:
                consumer(msg)

        return self.add_task(f"sub_{channel_id}", sub_func)

    def run(self) -> int:
        """Execute all tasks in dependency order.

        Returns the number of tasks executed.
        """
        # Topological sort
        executed = 0
        remaining = list(self._tasks)

        while remaining:
            ready = [t for t in remaining
                     if all(d.is_complete for d in t._dependencies)]
            if not ready:
                raise RuntimeError("Cycle detected in task dependencies")

            for t in ready:
                t.execute()
                executed += 1
                remaining.remove(t)

        self._stats.runs += 1
        self._stats.total_tasks += executed
        return executed

    def reset(self):
        """Reset all tasks for the next run cycle."""
        for t in self._tasks:
            t.reset()

    @property
    def task_count(self) -> int:
        return len(self._tasks)

    @property
    def channel_count(self) -> int:
        return len(self._channels)

    @property
    def stats(self) -> 'FlowStats':
        return self._stats


class FlowStats:
    """StreamTaskFlow execution statistics."""
    __slots__ = ('runs', 'total_tasks')

    def __init__(self):
        self.runs = 0
        self.total_tasks = 0
