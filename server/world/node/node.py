# SPDX-License-Identifier: Apache-2.0
#
# world/node/node.py
#
# Node — 个体的世界接口
#
# CyberRT来源:
#   cyber/node/node.h → Node (节点, 个体的通信入口)
#   cyber/node/node_channel_impl.h → NodeChannelImpl (通道实现)
#
# 语义:
#   Node = 个体在世界中的"身份证"。
#   个体通过Node发布MotionRequest、订阅ConfirmedState。
#   Node封装了transport层的Writer/Reader创建。
#
# governance对应:
#   AgentMesh.Agent — agent的网格身份
#   identity/attestation + lifecycle management
#
# 使用:
#   node = Node("individual_1", individual_id=1, channel_manager=cm)
#   writer = node.create_writer("/world/motion_request", MotionRequest)
#   reader = node.create_reader("/world/confirmed_state", ConfirmedState,
#                               callback=on_confirmed)
#   writer.write(request)

from typing import Callable, Optional, Dict, List, Any
from world.proto.channels import ChannelManager, Writer, Reader
from world.service_discovery.topology import (
    TopologyManager, NodeAttributes,
)


class Node:
    """
    个体节点 — 世界中每个个体的通信入口

    CyberRT来源 (cyber/node/node.h):
      Node(node_name) — 创建节点
      CreateWriter<T>(channel_name) → Writer
      CreateReader<T>(channel_name, callback) → Reader
      GetNodeName() → string

    lifecycle:
      1. 创建Node → 注册到TopologyManager
      2. CreateWriter/CreateReader → 创建通道连接
      3. 通过Writer发布MotionRequest
      4. 通过Reader订阅ConfirmedState
      5. 销毁Node → 从TopologyManager注销

    governance对应:
      Node ≈ AgentMesh.Agent
      CreateWriter ≈ agent.send()
      CreateReader ≈ agent.subscribe()
      TopologyManager注册 ≈ AgentMesh.register_agent()
    """

    def __init__(self, node_name: str,
                 individual_id: int,
                 channel_manager: ChannelManager):
        self._node_name = node_name
        self._individual_id = individual_id
        self._channel_manager = channel_manager
        self._writers: Dict[str, Writer] = {}
        self._readers: Dict[str, Reader] = {}

        # 注册到拓扑管理器
        topo = TopologyManager.instance()
        self._node_attr = NodeAttributes(
            node_name=node_name,
            individual_id=individual_id,
        )
        topo.node_manager.join(self._node_attr)

    def create_writer(self, channel_name: str, msg_type: type) -> Writer:
        """
        创建通道写入者

        CyberRT: auto writer = node->CreateWriter<T>(channel_name)
        """
        writer = self._channel_manager.create_writer(channel_name, msg_type)
        self._writers[channel_name] = writer

        # 注册发布关系
        topo = TopologyManager.instance()
        topo.register_publisher(self._node_name, channel_name)
        self._node_attr.channels_pub.append(channel_name)

        return writer

    def create_reader(self, channel_name: str, msg_type: type,
                      callback: Callable = None) -> Reader:
        """
        创建通道读取者

        CyberRT: auto reader = node->CreateReader<T>(channel_name, callback)
        """
        reader = self._channel_manager.create_reader(
            channel_name, msg_type, callback=callback
        )
        self._readers[channel_name] = reader

        # 注册订阅关系
        topo = TopologyManager.instance()
        topo.register_subscriber(self._node_name, channel_name)
        self._node_attr.channels_sub.append(channel_name)

        return reader

    def get_writer(self, channel_name: str) -> Optional[Writer]:
        return self._writers.get(channel_name)

    def get_reader(self, channel_name: str) -> Optional[Reader]:
        return self._readers.get(channel_name)

    @property
    def node_name(self) -> str:
        return self._node_name

    @property
    def individual_id(self) -> int:
        return self._individual_id

    def shutdown(self):
        """关闭节点 — 从拓扑管理器注销"""
        topo = TopologyManager.instance()
        topo.node_manager.leave(self._node_name)


class Individual:
    """
    个体 — 世界中的一个实体

    这是面向用户的高级API，封装Node + MotionRequest发布 + ConfirmedState订阅。

    语义:
      个体不能单方面决定自己的位置。
      个体只能"申请"运动(request_move)，
      世界解算器(WorldResolver)确认后，confirmed_state回调被触发。

    使用:
      individual = Individual("agent_1", id=1, channel_manager=cm)
      individual.on_confirmed = lambda state: print(f"位置确认: {state.position}")
      individual.request_move(delta_position=Vec3(1, 0, 0))

    governance对应:
      Individual ≈ governance Agent
      request_move ≈ agent 发起 ApprovalRequest
      on_confirmed ≈ agent 收到 ExecutionDecision
    """

    def __init__(self, name: str, individual_id: int,
                 channel_manager: ChannelManager):
        from world.proto.world_types import (
            MotionRequest, ConfirmedState, WorldTick,
            Vec3, Quat, CollisionLayer,
        )
        from world.proto.channels import (
            CHANNEL_MOTION_REQUEST, CHANNEL_CONFIRMED_STATE,
            CHANNEL_WORLD_TICK,
        )

        self._id = individual_id
        self._name = name
        self._current_tick_seq = 0

        # 创建Node
        self._node = Node(name, individual_id, channel_manager)

        # 创建 MotionRequest writer
        self._request_writer = self._node.create_writer(
            CHANNEL_MOTION_REQUEST, MotionRequest
        )

        # 订阅 ConfirmedState — 只关心自己的
        self._confirmed_state: Optional[ConfirmedState] = None
        self._node.create_reader(
            CHANNEL_CONFIRMED_STATE, ConfirmedState,
            callback=self._on_confirmed_state
        )

        # 订阅 WorldTick — 获取当前tick_seq
        self._node.create_reader(
            CHANNEL_WORLD_TICK, WorldTick,
            callback=self._on_tick
        )

        # 用户回调
        self.on_confirmed: Optional[Callable[[ConfirmedState], None]] = None
        self.on_tick: Optional[Callable[[WorldTick], None]] = None

    def _on_tick(self, tick):
        self._current_tick_seq = tick.tick_seq
        if self.on_tick:
            self.on_tick(tick)

    def _on_confirmed_state(self, state):
        if state.individual_id == self._id:
            self._confirmed_state = state
            if self.on_confirmed:
                self.on_confirmed(state)

    def request_move(self, delta_position=None, delta_rotation=None,
                     priority: float = 0.0,
                     collision_mask: int = None):
        """
        申请运动 — 提交MotionRequest到世界

        个体只能申请，不能决定。
        世界解算器确认后，on_confirmed回调被触发。
        """
        from world.proto.world_types import (
            MotionRequest, Vec3, Quat, CollisionLayer,
        )

        request = MotionRequest(
            individual_id=self._id,
            tick_seq=self._current_tick_seq,
            delta_position=delta_position or Vec3(),
            delta_rotation=delta_rotation or Quat(),
            priority=priority,
            collision_mask=collision_mask if collision_mask is not None
                           else CollisionLayer.DEFAULT,
        )
        self._request_writer.write(request)
        return request

    @property
    def confirmed_position(self):
        if self._confirmed_state:
            return self._confirmed_state.position
        return None

    @property
    def individual_id(self) -> int:
        return self._id

    @property
    def name(self) -> str:
        return self._name

    def shutdown(self):
        self._node.shutdown()
