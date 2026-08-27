# SPDX-License-Identifier: Apache-2.0
#
# world/component/dynamic.py
#
# 动态组件基类 — 运行时可增减个体的组件
#
# CyberRT来源:
#   cyber/component/component.h → Component<M0, M1, ...> (静态, 编译期确定通道)
#   没有动态组件的概念 — CyberRT的Component在启动时确定, 运行时不变。
#
# pubsub-loop扩展:
#   世界中的个体是动态的 — 随时可以出生/消亡。
#   DynamicComponentBase 管理一个动态的 individual 集合,
#   支持运行时 register/unregister。
#
# governance来源:
#   AgentMesh — 动态的 agent 集合
#   Session.participants — 动态的参与者列表
#   Runtime.register_agent(identity) / remove_agent(identity)
#
# PRD映射:
#   #48 → 并发哈希表迁移到订阅者注册表 (无锁并发索引)
#   #118 → CoopLaunch 协作式 pub/sub 分发
#
# 关键约束:
#   1. register/unregister 可能和 Proc() 并发发生
#   2. 注册表查找在每个 tick 的 Proc() 中发生 (高频路径)
#   3. 1000个个体, 每tick 1000次查找 → 需要 O(1) 查找
#   4. 从 std::mutex + unordered_map 迁移到 AtomicHashMap (PRD #48)

import threading
from typing import Dict, Optional, List, Callable, Any, Set
from abc import abstractmethod

from world.base.primitives import AtomicHashMap
from world.component.component import ComponentBase
from world.node.node import Node


class SubscriberInfo:
    """
    订阅者信息 — 描述一个已注册的个体

    对应 governance:
      AgentIdentity — agent_did, display_name, trust_ring
    """
    __slots__ = ('individual_id', 'name', 'channels', 'priority',
                 'trust_ring', 'registered_tick')

    def __init__(self, individual_id: int, name: str = "",
                 channels: List[str] = None, priority: int = 0,
                 trust_ring: int = 2):
        self.individual_id = individual_id
        self.name = name
        self.channels = channels or []
        self.priority = priority
        self.trust_ring = trust_ring  # governance: ExecutionRing
        self.registered_tick = 0


class DynamicComponentBase(ComponentBase):
    """
    动态组件基类 — 支持运行时个体增减

    PRD #48: 使用 AtomicHashMap (并发安全哈希表) 替代
    std::mutex + unordered_map, 提升并发查找性能。

    数据结构:
      _registry: AtomicHashMap<int, SubscriberInfo>
        key = individual_id
        value = SubscriberInfo (订阅信息)

    性能:
      register(): O(1) — AtomicHashMap.insert()
      unregister(): O(1) — AtomicHashMap.remove()
      lookup(): O(1) — AtomicHashMap.get()
      iterate(): O(n) — AtomicHashMap.values()

    并发安全:
      AtomicHashMap 内部用 RLock 保护。
      Python 的 GIL 限制了真正的无锁,
      但 RLock 比 Mutex 更灵活(支持递归锁)。

    governance对应:
      类似 AgentMesh 的动态 agent 注册表:
        mesh.register_agent(identity)
        mesh.remove_agent(identity)
        mesh.list_agents() → [Agent]
    """

    def __init__(self, node: Node = None, name: str = ""):
        super().__init__(node, name)
        # PRD #48: 用 AtomicHashMap 替代 dict + mutex
        self._registry: AtomicHashMap = AtomicHashMap()
        # 变更通知回调
        self._on_register_callbacks: List[Callable[[SubscriberInfo], None]] = []
        self._on_unregister_callbacks: List[Callable[[int], None]] = []

    def register_individual(self, individual_id: int,
                            name: str = "",
                            channels: List[str] = None,
                            priority: int = 0,
                            trust_ring: int = 2) -> bool:
        """
        注册个体到动态组件

        governance对应: AgentMesh.register_agent(identity)

        PRD #48: 使用 AtomicHashMap.insert() — O(1)
        旧实现: mutex.lock() → map.insert() → mutex.unlock()
        新实现: AtomicHashMap.insert() (内部 RLock)
        """
        info = SubscriberInfo(
            individual_id=individual_id,
            name=name,
            channels=channels,
            priority=priority,
            trust_ring=trust_ring,
        )
        ok = self._registry.insert(individual_id, info)
        if ok:
            for cb in self._on_register_callbacks:
                cb(info)
        return ok

    def unregister_individual(self, individual_id: int) -> bool:
        """
        从动态组件注销个体

        governance对应: AgentMesh.remove_agent(identity)
        """
        ok = self._registry.remove(individual_id)
        if ok:
            for cb in self._on_unregister_callbacks:
                cb(individual_id)
        return ok

    def get_individual(self, individual_id: int) -> Optional[SubscriberInfo]:
        """
        查找个体 — O(1)

        这是高频路径: 每个 tick 的 Proc() 中,
        WorldResolver 需要查找每个 MotionRequest 的发送者信息。
        1000个体 × 30Hz = 30,000次/秒。
        """
        return self._registry.get(individual_id)

    def has_individual(self, individual_id: int) -> bool:
        return self._registry.contains(individual_id)

    def all_individuals(self) -> List[SubscriberInfo]:
        """
        获取所有已注册个体

        governance对应: AgentMesh.list_agents()
        """
        return self._registry.values()

    def individual_ids(self) -> List[int]:
        return self._registry.keys()

    @property
    def individual_count(self) -> int:
        return self._registry.size

    def on_register(self, callback: Callable[[SubscriberInfo], None]):
        """注册 "新个体加入" 回调"""
        self._on_register_callbacks.append(callback)

    def on_unregister(self, callback: Callable[[int], None]):
        """注册 "个体离开" 回调"""
        self._on_unregister_callbacks.append(callback)

    @abstractmethod
    def proc_individual(self, individual_id: int, *messages) -> bool:
        """
        处理单个个体 — 子类实现

        DynamicComponentBase 的 Proc() 会遍历所有已注册个体,
        对每个个体调用 proc_individual()。
        """
        ...

    def proc(self, *messages) -> bool:
        """
        处理一个tick — 遍历所有已注册个体

        PRD #118: CoopLaunch 协作式分发
          当前是串行遍历。
          后续可以用 Scheduler.create_task_batch() 并行处理。
        """
        ids = self.individual_ids()
        for iid in ids:
            self.proc_individual(iid, *messages)
        return True

    def filter_by_trust_ring(self, max_ring: int) -> List[SubscriberInfo]:
        """
        按信任环过滤 — governance 集成

        governance:
          RING_0_KERNEL = 0 (最高信任)
          RING_1_SUPERVISOR = 1
          RING_2_USER = 2
          RING_3_SANDBOX = 3 (最低信任)

        返回信任等级 <= max_ring 的个体。
        """
        return [info for info in self._registry.values()
                if info.trust_ring <= max_ring]
