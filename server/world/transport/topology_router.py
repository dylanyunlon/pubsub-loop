"""
world.transport.topology_router — topology-distance-based transport routing
==============================================================================

PRD #17: Refactor Transport layer channel dispatch logic to automatically
route through RTPS / shared memory / intra-process based on topology distance
and state message size.

Problem: Current transport selection is manual (caller chooses INTRA/SHM/RTPS).
When individuals move between process groups or network nodes, the transport
mode doesn't adapt, causing latency spikes or unnecessary serialization.

Solution: TopologyRouter inspects the topology graph to determine the distance
between sender and receiver, then selects the optimal transport:
  - Same process (distance=0): Intra-process (zero-copy reference)
  - Same host, different process (distance=1): Shared memory
  - Different host (distance=2): RTPS network transport

Sources:
  - CyberRT: transport/transport, transport/dispatcher
  - pubsub-loop: transport/transmitter.py Transport.create_transmitter
"""

from __future__ import annotations

import struct
from enum import IntEnum
from typing import Any, Callable, Dict, List, Optional, Set, Tuple, Type, TypeVar

from world.transport.transmitter import (
    Transport, TransportMode, Transmitter, Receiver,
    IntraTransmitter, IntraDispatcher,
)

T = TypeVar('T')


class TopologyDistance(IntEnum):
    """Distance between two endpoints in the topology graph."""
    SAME_THREAD = 0       # same thread — direct reference
    SAME_PROCESS = 1      # same process, different thread — intra dispatch
    SAME_HOST = 2         # same host, different process — shared memory
    REMOTE = 3            # different host — RTPS/network


# Message size thresholds for transport selection
_SHM_SIZE_THRESHOLD = 256           # bytes: messages > this prefer SHM over RTPS
_INTRA_DIRECT_THRESHOLD = 64 * 1024  # bytes: messages > this always use intra if possible


class TopologyRouter:
    """Route messages through optimal transport based on topology distance.

    Usage::

        router = TopologyRouter()
        router.register_endpoint('sensor_node', process_id=1, host='host1')
        router.register_endpoint('fusion_node', process_id=1, host='host1')

        mode = router.select_transport(
            sender='sensor_node',
            receiver='fusion_node',
            msg_size=1024,
        )
        # → TransportMode.SHM (same host, different process... wait, same process)
        # Actually same process → TransportMode.INTRA
    """

    def __init__(self):
        self._endpoints: Dict[str, EndpointInfo] = {}
        self._overrides: Dict[Tuple[str, str], TransportMode] = {}
        self._stats = RouterStats()

    def register_endpoint(
        self,
        endpoint_id: str,
        process_id: int = 0,
        host: str = 'localhost',
        thread_id: int = 0,
    ):
        """Register an endpoint for routing decisions."""
        self._endpoints[endpoint_id] = EndpointInfo(
            endpoint_id=endpoint_id,
            process_id=process_id,
            host=host,
            thread_id=thread_id,
        )

    def unregister_endpoint(self, endpoint_id: str):
        self._endpoints.pop(endpoint_id, None)

    def compute_distance(
        self, sender_id: str, receiver_id: str,
    ) -> TopologyDistance:
        """Compute topology distance between two endpoints."""
        sender = self._endpoints.get(sender_id)
        receiver = self._endpoints.get(receiver_id)

        if sender is None or receiver is None:
            return TopologyDistance.REMOTE  # conservative default

        if sender.host != receiver.host:
            return TopologyDistance.REMOTE
        if sender.process_id != receiver.process_id:
            return TopologyDistance.SAME_HOST
        if sender.thread_id == receiver.thread_id:
            return TopologyDistance.SAME_THREAD
        return TopologyDistance.SAME_PROCESS

    def select_transport(
        self,
        sender: str,
        receiver: str,
        msg_size: int = 0,
        channel_name: str = '',
    ) -> TransportMode:
        """Select optimal transport mode for a sender-receiver pair.

        Decision matrix:
          distance=SAME_THREAD  → INTRA (zero-copy direct reference)
          distance=SAME_PROCESS → INTRA (intra-process dispatch)
          distance=SAME_HOST    → SHM if msg_size > threshold, else RTPS
          distance=REMOTE       → RTPS
        """
        # Check explicit overrides first
        override = self._overrides.get((sender, receiver))
        if override is not None:
            self._stats.override_hits += 1
            return override

        distance = self.compute_distance(sender, receiver)
        self._stats.route_decisions += 1

        if distance <= TopologyDistance.SAME_PROCESS:
            self._stats.intra_routes += 1
            return TransportMode.INTRA

        if distance == TopologyDistance.SAME_HOST:
            if msg_size >= _SHM_SIZE_THRESHOLD:
                self._stats.shm_routes += 1
                return TransportMode.SHM
            self._stats.rtps_routes += 1
            return TransportMode.RTPS

        # REMOTE
        self._stats.rtps_routes += 1
        return TransportMode.RTPS

    def set_override(
        self, sender: str, receiver: str, mode: TransportMode,
    ):
        """Force a specific transport mode for a sender-receiver pair."""
        self._overrides[(sender, receiver)] = mode

    def clear_override(self, sender: str, receiver: str):
        self._overrides.pop((sender, receiver), None)

    def all_routes(
        self, sender: str,
    ) -> Dict[str, TransportMode]:
        """Get optimal transport for a sender to all known endpoints."""
        result = {}
        for recv_id in self._endpoints:
            if recv_id != sender:
                result[recv_id] = self.select_transport(sender, recv_id)
        return result

    @property
    def stats(self) -> 'RouterStats':
        return self._stats

    @property
    def endpoint_count(self) -> int:
        return len(self._endpoints)


class EndpointInfo:
    """Topology endpoint metadata."""
    __slots__ = ('endpoint_id', 'process_id', 'host', 'thread_id')

    def __init__(
        self,
        endpoint_id: str,
        process_id: int = 0,
        host: str = 'localhost',
        thread_id: int = 0,
    ):
        self.endpoint_id = endpoint_id
        self.process_id = process_id
        self.host = host
        self.thread_id = thread_id


class RouterStats:
    """Routing decision statistics."""
    __slots__ = (
        'route_decisions', 'intra_routes', 'shm_routes',
        'rtps_routes', 'override_hits',
    )

    def __init__(self):
        self.route_decisions = 0
        self.intra_routes = 0
        self.shm_routes = 0
        self.rtps_routes = 0
        self.override_hits = 0

    def summary(self) -> str:
        total = self.route_decisions or 1
        return (
            f"Routes: {self.route_decisions} total | "
            f"INTRA: {self.intra_routes} ({100*self.intra_routes/total:.0f}%) | "
            f"SHM: {self.shm_routes} ({100*self.shm_routes/total:.0f}%) | "
            f"RTPS: {self.rtps_routes} ({100*self.rtps_routes/total:.0f}%) | "
            f"Overrides: {self.override_hits}"
        )
