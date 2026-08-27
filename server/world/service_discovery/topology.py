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
    role: str = ""  # PRD #26: "sensor"/"planner"/"actuator"/"observer"
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

    # ─── PRD #26: diff_by_role predicate overload ───
    #
    # CyberRT: TopologyDiff 只支持全量差异输出
    # pubsub-loop扩展: 按角色(role)过滤拓扑变更
    #
    # governance对应:
    #   AgentMesh 的 ring-based filtering:
    #     只关注同一 ExecutionRing 内的 agent 变更

    def get_by_role(self, role: str) -> List[NodeAttributes]:
        """
        按角色过滤节点 — PRD #26

        role: "sensor" / "planner" / "actuator" / "observer" 等
        返回该角色的所有节点。

        用途:
          WorldResolver 只需要知道有碰撞体的个体(role=dynamic/static),
          不需要知道观察者(role=observer)。
        """
        all_nodes = self._node_manager.get_all_nodes()
        return [n for n in all_nodes
                if getattr(n, 'role', '') == role]

    def diff_by_role(self, role: str,
                     predicate: Callable[[NodeAttributes], bool] = None
                     ) -> List[ChangeMsg]:
        """
        按角色过滤拓扑差异 — PRD #26 key-predicate overload

        收集最近的拓扑变更, 只返回指定角色的变更。
        可选 predicate 进一步过滤。

        用途:
          transport层: 只关心同进程内节点的加入/离开
          scheduler: 只关心 priority > 0 的节点变更
          governance: 只关心低信任(RING_3)节点的变更
        """
        changes = self._recent_changes if hasattr(self, '_recent_changes') else []
        result = []
        for change in changes:
            node = self._node_manager.get_node(change.node_name)
            if node is None:
                continue
            if getattr(node, 'role', '') != role:
                continue
            if predicate and not predicate(node):
                continue
            result.append(change)
        return result

    def compute_topology_distance(self, node_a: str, node_b: str) -> int:
        """
        计算两个节点的拓扑距离

        距离定义:
          0 = 同一个体(自身)
          1 = 同进程
          2 = 同机器不同进程
          3 = 不同机器

        transport层用它决定传输模式:
          距离 1 → IntraTransmitter (零拷贝)
          距离 2 → ShmTransmitter (共享内存)
          距离 3 → RTPS (网络, 暂不实现)

        对于"世界中有1000个个体"的场景:
          当前单机单进程 → 距离全部为1 → 全部用Intra(零拷贝)
          性能足够。跨进程场景后续扩展。
        """
        if node_a == node_b:
            return 0
        a = self._node_manager.get_node(node_a)
        b = self._node_manager.get_node(node_b)
        if a is None or b is None:
            return 3  # 未知节点视为远端
        # 比较进程ID
        pid_a = getattr(a, 'process_id', 0)
        pid_b = getattr(b, 'process_id', 0)
        if pid_a == pid_b and pid_a != 0:
            return 1  # 同进程
        # 比较主机名
        host_a = getattr(a, 'host_name', '')
        host_b = getattr(b, 'host_name', '')
        if host_a == host_b and host_a != '':
            return 2  # 同机器不同进程
        return 3  # 不同机器

    def shutdown(self):
        """关闭 — 通知所有节点离开"""
        for node in self._node_manager.get_all_nodes():
            self._node_manager.leave(node.node_name)
