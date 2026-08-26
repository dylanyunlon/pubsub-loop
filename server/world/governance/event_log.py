# SPDX-License-Identifier: Apache-2.0
#
# world/governance/event_log.py
#
# EventLog — 世界事件审计日志
#
# governance来源:
#   HypervisorEventBus (event_bus.py):
#     - typed event (EventType枚举)
#     - append-only deque
#     - subscribe by type / by session / by agent
#     - 支持回放
#
# CyberRT映射 (PRD #1649):
#   governance: EventBus.emit(event) → 内存deque + notify subscribers
#   cyberRT:    EventLog.record(event) → channel pub + 持久化(可选)
#
#   差异:
#     CyberRT 用 channel 做事件传播(channel是一等公民)
#     governance 用独立的 EventBus 做审计层(append-only store)
#     这里融合两者: 用 channel 传播 + 本地 append-only 存储

import hashlib
import json
import time
from collections import deque
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Callable, Dict, List, Optional, Deque

from world.proto.world_types import ConfirmedState
from world.proto.channels import (
    ChannelManager, Writer, CHANNEL_GOVERNANCE_LOG
)


# ─── 事件类型 (来自 governance EventType 枚举) ───

class EventType(Enum):
    """
    事件类型枚举

    governance来源: HypervisorEventBus 的 EventType
    新增 CyberRT 特有类型: TICK, COLLISION, RESOLVE
    """
    # governance 原有类型
    SESSION_CREATED  = auto()
    SESSION_JOINED   = auto()
    SESSION_LEFT     = auto()
    ACTION_REQUESTED = auto()
    ACTION_APPROVED  = auto()
    ACTION_DENIED    = auto()
    TRUST_UPDATED    = auto()

    # CyberRT 新增类型
    TICK_STARTED     = auto()
    TICK_COMPLETED   = auto()
    COLLISION        = auto()
    STATE_CONFIRMED  = auto()
    GOVERNANCE_EVAL  = auto()


@dataclass
class WorldEvent:
    """
    世界事件 — governance HypervisorEvent 的 CyberRT 适配

    governance字段:
      event_type, session_id, agent_did, payload, causal_trace_id
    CyberRT字段:
      tick_seq, individual_id
    """
    event_type: EventType
    tick_seq: int = 0
    individual_id: Optional[int] = None
    payload: Dict = field(default_factory=dict)
    timestamp_ns: int = field(default_factory=time.time_ns)

    # governance: causal_trace_id — 用于因果链追踪
    trace_id: Optional[str] = None


class EventLog:
    """
    世界事件审计日志

    governance来源:
      HypervisorEventBus:
        emit(event) → deque.append + notify
        query_by_type(type) → filter
        query_by_session(session_id) → filter
        replay(from_seq) → iterate

    CyberRT融合:
      record() → append-only存储 + channel pub(可选)
      DeltaEngine: 每个tick的confirmed_states → hash chain

    PRD #1649:
      governance的delta hash chain可以直接用于CyberRT的confirmed_state:
      每个tick的所有confirmed_state组成一个delta,计算hash chain root。
    """

    def __init__(self, channel_manager: Optional[ChannelManager] = None,
                 max_events: int = 100_000):
        self._events: Deque[WorldEvent] = deque(maxlen=max_events)
        self._subscribers: Dict[EventType, List[Callable[[WorldEvent], None]]] = {}
        self._hash_chain: List[str] = []  # governance DeltaEngine

        # 可选: 通过channel发布事件
        self._channel_writer: Optional[Writer] = None
        if channel_manager:
            self._channel_writer = channel_manager.create_writer(
                CHANNEL_GOVERNANCE_LOG, WorldEvent
            )

    def record(self, event: WorldEvent):
        """
        记录事件 — governance EventBus.emit()

        append-only: 事件一旦记录不可修改/删除
        """
        self._events.append(event)

        # 通知按类型订阅的处理器
        if event.event_type in self._subscribers:
            for handler in self._subscribers[event.event_type]:
                handler(event)

        # 通过channel发布(CyberRT模式)
        if self._channel_writer:
            self._channel_writer.write(event)

    def subscribe(self, event_type: EventType,
                  handler: Callable[[WorldEvent], None]):
        """
        按类型订阅事件 — governance EventBus.subscribe(type, handler)
        """
        if event_type not in self._subscribers:
            self._subscribers[event_type] = []
        self._subscribers[event_type].append(handler)

    def query_by_type(self, event_type: EventType) -> List[WorldEvent]:
        """governance: query_by_type"""
        return [e for e in self._events if e.event_type == event_type]

    def query_by_individual(self, individual_id: int) -> List[WorldEvent]:
        """governance: query_by_agent (agent_did → individual_id)"""
        return [e for e in self._events if e.individual_id == individual_id]

    def query_by_tick(self, tick_seq: int) -> List[WorldEvent]:
        """CyberRT特有: 按tick查询"""
        return [e for e in self._events if e.tick_seq == tick_seq]

    # ── DeltaEngine: hash chain (governance审计) ──

    def capture_tick_delta(self, tick_seq: int,
                           confirmed_states: List[ConfirmedState]):
        """
        计算本tick的delta hash — governance DeltaEngine

        PRD #1649:
          governance: delta_engine.capture(before, after) → hash chain
          cyberRT:    tick_audit.capture(tick_seq, [confirmed_states]) → hash chain

        每个tick的所有confirmed_state序列化 → SHA-256
        链式: hash(prev_hash + tick_hash)
        """
        # 序列化本tick的所有confirmed_state
        tick_data = json.dumps([
            {
                "id": s.individual_id,
                "tick": s.tick_seq,
                "pos": [s.position.x, s.position.y, s.position.z],
                "flags": s.resolve_flags,
            }
            for s in sorted(confirmed_states, key=lambda s: s.individual_id)
        ], sort_keys=True)

        tick_hash = hashlib.sha256(tick_data.encode()).hexdigest()

        # 链式hash
        prev = self._hash_chain[-1] if self._hash_chain else "0" * 64
        chain_hash = hashlib.sha256(
            f"{prev}{tick_hash}".encode()
        ).hexdigest()

        self._hash_chain.append(chain_hash)

        # 记录审计事件
        self.record(WorldEvent(
            event_type=EventType.TICK_COMPLETED,
            tick_seq=tick_seq,
            payload={"hash": chain_hash, "state_count": len(confirmed_states)},
        ))

        return chain_hash

    @property
    def hash_chain(self) -> List[str]:
        """获取完整的hash chain — 用于审计验证"""
        return list(self._hash_chain)

    @property
    def event_count(self) -> int:
        return len(self._events)
