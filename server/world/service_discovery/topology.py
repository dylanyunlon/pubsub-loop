# SPDX-License-Identifier: Apache-2.0
#
# world/service_discovery/topology.py
#
# 拓扑管理 — 从 CyberRT cyber/service_discovery 移植
#
# CyberRT来源:
#   cyber/service_discovery/topology_manager.h → TopologyManager (拓扑管理器)
#   cyber/service_discovery/specific_manager/node_manager.h → NodeManager
#   cyber/service_discovery/specific_manager/channel_manager.h → ChannelManager
#
# 职责:
#   TopologyManager: 管理世界中所有个体(Node)的注册/发现/离开
#   NodeManager: 管理Node的Join/Leave/HasNode/GetNodes
#   ChannelManager: 管理Channel的注册/发现(哪些Node发布/订阅哪些Channel)
#
# PRD #26: service_discovery的拓扑管理
#
# 还不知道的事:
#   service_discovery的拓扑管理对"世界中有1000个个体"的场景够不够用
#   → 分析:
#     CyberRT的TopologyManager用RTPS participant广播Join/Leave,
#     所有Node收到变更通知。对于1000个个体:
#     - Join/Leave事件: O(1000)条通知广播 — 可接受(只在启动/关闭时)
#     - 运行时查询: GetNodes() → 遍历list — O(1000) — 可接受
#     - channel订阅: 每个个体订阅 ~3个channel — 总共3000条注册 — 可接受
#     瓶颈: 如果个体频繁Join/Leave(动态创建/销毁), 广播风暴。
#     方案: 批量Join (batch_join), 30Hz tick对齐Join/Leave。
#
# governance对应:
#   AgentMesh — agent的注册/发现
#   Session.participants — 参与者列表

import threading
import time
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Dict, List, Optional, Callable, Set
from world.base.primitives import AtomicHashMap, Signal


class ChangeType(Enum):
    """拓扑变更类型"""
    JOIN  = auto()
    LEAVE = auto()
    CHANGE = auto()


@dataclass
class NodeAttributes:
    """
    节点属性 — 对应 CyberRT RoleAttributes

    CyberRT:
      RoleAttributes: channel_name, channel_id, host_name, process_id, node_name, node_id
    pubsub-loop:
      node_name → individual的标识名
      individual_id → 个体ID
      channels → 该个体发布/订阅的通道列表
    """
    node_name: str
    individual_id: int
    host_name: str = "localhost"
    process_id: int = 0
    channels_pub: List[str] = field(default_factory=list)
    channels_sub: List[str] = field(default_factory=list)
    join_time_ns: int = field(default_factory=time.time_ns)


@dataclass
class ChangeMsg:
    """拓扑变更消息"""
    change_type: ChangeType
    node_attr: NodeAttributes
    timestamp_ns: int = field(default_factory=time.time_ns)


class NodeManager:
    """
    节点管理器 — 管理个体的注册/离开/查询

    CyberRT来源 (node_manager.h):
      Join(attr) → 注册新节点
      Leave(attr) → 移除节点
      HasNode(name) → 查询节点是否存在
      GetNodes(nodes) → 获取所有节点

    1000个体场景:
      - 内存: NodeAttributes ~200 bytes × 1000 = 200KB — 可忽略
      - Join/Leave: 一次性注册1000个体 → 1000次字典插入 — <1ms
      - 查询: GetNodes() → list(dict.values()) — O(n), <0.1ms
    """

    def __init__(self):
        self._nodes = AtomicHashMap()  # node_name → NodeAttributes
        self._id_index: Dict[int, str] = {}  # individual_id → node_name 反向索引
        self._change_signal = Signal()  # 拓扑变更信号
        self._lock = threading.Lock()

    def join(self, attr: NodeAttributes) -> bool:
        """注册节点 — CyberRT: DisposeJoin"""
        if self._nodes.contains(attr.node_name):
            return False
        self._nodes.set(attr.node_name, attr)
        with self._lock:
            self._id_index[attr.individual_id] = attr.node_name
        self._change_signal.emit(ChangeMsg(
            change_type=ChangeType.JOIN,
            node_attr=attr
        ))
        return True

    def leave(self, node_name: str) -> bool:
        """移除节点 — CyberRT: DisposeLeave"""
        attr = self._nodes.get(node_name)
        if attr is None:
            return False
        self._nodes.remove(node_name)
        with self._lock:
            self._id_index.pop(attr.individual_id, None)
        self._change_signal.emit(ChangeMsg(
            change_type=ChangeType.LEAVE,
            node_attr=attr
        ))
        return True

    def has_node(self, node_name: str) -> bool:
        """CyberRT: HasNode"""
        return self._nodes.contains(node_name)

    def get_node(self, node_name: str) -> Optional[NodeAttributes]:
        return self._nodes.get(node_name)

    def get_node_by_id(self, individual_id: int) -> Optional[NodeAttributes]:
        with self._lock:
            name = self._id_index.get(individual_id)
        if name is None:
            return None
        return self._nodes.get(name)

    def get_all_nodes(self) -> List[NodeAttributes]:
        """CyberRT: GetNodes"""
        return self._nodes.values()

    def get_node_count(self) -> int:
        return self._nodes.size

    def on_change(self, callback: Callable[[ChangeMsg], None]) -> int:
        """注册拓扑变更回调"""
        return self._change_signal.connect(callback)

    def batch_join(self, attrs: List[NodeAttributes]) -> int:
        """
        批量注册 — 减少通知风暴

        对于1000个个体同时注册, 不要逐个Join通知,
        而是批量注册后发一次"batch join"通知。
        """
        count = 0
        for attr in attrs:
            if not self._nodes.contains(attr.node_name):
                self._nodes.set(attr.node_name, attr)
                with self._lock:
                    self._id_index[attr.individual_id] = attr.node_name
                count += 1
        # 批量通知(只发一次)
        if count > 0:
            self._change_signal.emit(ChangeMsg(
                change_type=ChangeType.JOIN,
                node_attr=NodeAttributes(
                    node_name=f"batch_{count}",
                    individual_id=-1,
                ),
            ))
        return count


class TopologyManager:
    """
    拓扑管理器 — 世界节点拓扑的总管理器

    CyberRT来源 (topology_manager.h):
      node_manager() → NodeManager
      channel_manager() → ChannelManager
      AddChangeListener(callback) → 注册变更回调

    pubsub-loop扩展:
      除了CyberRT的Node/Channel拓扑,还管理:
      1. 个体→通道的发布/订阅关系
      2. 个体之间的拓扑距离(同进程/跨进程/跨机器)
         → transport层用它决定 INTRA/SHM/RTPS

    governance对应:
      AgentMesh:
        register_agent(identity) → Join
        remove_agent(identity)   → Leave
        list_agents()            → GetNodes
    """

    _instance = None
    _lock = threading.Lock()

    @classmethod
    def instance(cls) -> 'TopologyManager':
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = cls()
        return cls._instance

    def __init__(self):
        self._node_manager = NodeManager()
        self._channel_subs: Dict[str, Set[str]] = {}  # channel → {node_names}
        self._channel_pubs: Dict[str, Set[str]] = {}  # channel → {node_names}
        self._change_signal = Signal()
        self._lock = threading.Lock()

        # 转发NodeManager的变更信号
        self._node_manager.on_change(
            lambda msg: self._change_signal.emit(msg)
        )

    @property
    def node_manager(self) -> NodeManager:
        return self._node_manager

    def register_publisher(self, node_name: str, channel_name: str):
        """注册: 某个Node发布某个Channel"""
        with self._lock:
            if channel_name not in self._channel_pubs:
                self._channel_pubs[channel_name] = set()
            self._channel_pubs[channel_name].add(node_name)

    def register_subscriber(self, node_name: str, channel_name: str):
        """注册: 某个Node订阅某个Channel"""
        with self._lock:
            if channel_name not in self._channel_subs:
                self._channel_subs[channel_name] = set()
            self._channel_subs[channel_name].add(node_name)

    def get_publishers(self, channel_name: str) -> List[str]:
        """查询某个Channel的所有发布者"""
        with self._lock:
            return list(self._channel_pubs.get(channel_name, set()))

    def get_subscribers(self, channel_name: str) -> List[str]:
        """查询某个Channel的所有订阅者"""
        with self._lock:
            return list(self._channel_subs.get(channel_name, set()))

    def on_change(self, callback: Callable[[ChangeMsg], None]) -> int:
        return self._change_signal.connect(callback)

    def shutdown(self):
        """关闭 — 通知所有节点离开"""
        for node in self._node_manager.get_all_nodes():
            self._node_manager.leave(node.node_name)
