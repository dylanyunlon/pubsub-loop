# SPDX-License-Identifier: Apache-2.0
#
# world/proto/channels.py
#
# Channel 定义 — 世界主循环的所有通道名称和类型绑定
#
# CyberRT来源:
#   cyber::Node::CreateWriter<T>(channel_name)
#   cyber::Node::CreateReader<T>(channel_name, callback)
#   transport层: INTRA(零拷贝) / SHM(共享内存) / RTPS(跨节点)
#
# governance来源:
#   HypervisorEventBus — typed event + subscribe_by_type
#
# 本模块定义通道名和类型绑定的注册表。
# pubsub-loop 的 transport 层（未来移植）根据这些通道名路由消息。

from dataclasses import dataclass
from typing import TypeVar, Generic, Callable, List, Optional, Dict, Any
from enum import Enum
from world.proto.world_types import (
    WorldTick, MotionRequest, ConfirmedState, CollisionEvent, GovernanceVerdict
)


# ─── 通道名常量 (对应 CyberRT 的 channel_name 字符串) ───

CHANNEL_WORLD_TICK       = "/world/tick"
CHANNEL_MOTION_REQUEST   = "/world/motion_request"     # wildcard: /world/motion_request/{id}
CHANNEL_CONFIRMED_STATE  = "/world/confirmed_state"    # wildcard: /world/confirmed_state/{id}
CHANNEL_COLLISION_EVENTS = "/world/collision/events"
CHANNEL_GOVERNANCE_LOG   = "/world/governance/log"


# ─── 传输模式 (对应 CyberRT OptionalMode 枚举) ───

class TransportMode(Enum):
    """
    传输模式选择

    CyberRT来源 (transport.h):
      INTRA  → IntraTransmitter (进程内零拷贝, 直接传指针)
      SHM    → ShmTransmitter   (共享内存, p99 < 1ms)
      RTPS   → RtpsTransmitter  (跨节点DDS协议)
      HYBRID → HybridTransmitter(按拓扑距离自动选择)

    governance来源:
      gRPC transport / WebSocket transport / base transport抽象
    """
    INTRA  = "intra"    # 进程内: 直接Python引用传递
    SHM    = "shm"      # 共享内存: multiprocessing.shared_memory
    HYBRID = "hybrid"   # 自动选择(默认)


# ─── Writer/Reader 抽象 (对应 CyberRT Node::CreateWriter/CreateReader) ───

T = TypeVar('T')


class Writer(Generic[T]):
    """
    通道写入者

    CyberRT来源:
      auto writer = node->CreateWriter<MotionRequest>("/world/motion_request")
      writer->Write(msg)

    在 pubsub-loop 中:
      writer = channel_manager.create_writer(CHANNEL_MOTION_REQUEST, MotionRequest)
      writer.write(request)
    """

    def __init__(self, channel_name: str, transport: TransportMode = TransportMode.INTRA):
        self.channel_name = channel_name
        self.transport = transport
        self._listeners: List[Callable[[T], None]] = []

    def write(self, msg: T) -> bool:
        """发布消息到通道 — 通知所有订阅者"""
        for listener in self._listeners:
            listener(msg)
        return True

    def _add_listener(self, callback: Callable[[T], None]):
        self._listeners.append(callback)


class Reader(Generic[T]):
    """
    通道读取者

    CyberRT来源:
      auto reader = node->CreateReader<ConfirmedState>(
          "/world/confirmed_state",
          [this](const auto& msg) { OnConfirmed(msg); }
      )

    在 pubsub-loop 中:
      reader = channel_manager.create_reader(
          CHANNEL_CONFIRMED_STATE, ConfirmedState, callback=on_confirmed
      )
    """

    def __init__(self, channel_name: str, callback: Callable[[T], None]):
        self.channel_name = channel_name
        self.callback = callback


class ChannelManager:
    """
    通道管理器 — 管理所有 Writer/Reader 的注册和消息路由

    CyberRT来源:
      NodeChannelImpl::CreateWriter / CreateReader
      + transport::Transport 的路由逻辑

    governance来源:
      HypervisorEventBus.subscribe(event_type, handler)

    当前实现: 纯进程内(INTRA模式)，所有通信通过Python回调。
    后续移植: 根据 PRD #17 transport层重构，增加SHM/RTPS模式。
    """

    def __init__(self):
        self._writers: Dict[str, Writer] = {}
        self._readers: Dict[str, List[Reader]] = {}

    def create_writer(self, channel_name: str, msg_type: type,
                      transport: TransportMode = TransportMode.INTRA) -> Writer:
        """创建通道写入者 — 对应 CyberRT Node::CreateWriter"""
        writer = Writer(channel_name, transport)
        self._writers[channel_name] = writer

        # 绑定已有的 reader
        if channel_name in self._readers:
            for reader in self._readers[channel_name]:
                writer._add_listener(reader.callback)

        return writer

    def create_reader(self, channel_name: str, msg_type: type,
                      callback: Callable) -> Reader:
        """创建通道读取者 — 对应 CyberRT Node::CreateReader"""
        reader = Reader(channel_name, callback)

        if channel_name not in self._readers:
            self._readers[channel_name] = []
        self._readers[channel_name].append(reader)

        # 如果 writer 已存在，绑定
        if channel_name in self._writers:
            self._writers[channel_name]._add_listener(callback)

        return reader
