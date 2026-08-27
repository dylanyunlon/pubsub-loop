# SPDX-License-Identifier: Apache-2.0
#
# world/engine.py
#
# WorldEngine — 世界引擎, 顶层启动器
#
# CyberRT来源:
#   cyber/mainboard/module_controller.h → 加载组件
#   cyber/mainboard/mainboard.cc → 启动框架, 初始化所有子系统
#   cyber/cyber.h → cyber::Init(), cyber::Shutdown()
#
# 语义:
#   WorldEngine 是整个世界引擎的入口。
#   它按正确顺序初始化所有子系统:
#     1. GlobalData (全局配置)
#     2. Clock (时钟)
#     3. ChannelManager (通道管理)
#     4. TopologyManager (拓扑管理)
#     5. Scheduler (调度器)
#     6. ParameterServer (参数服务)
#     7. ComponentManager → 注册组件:
#        - WorldTickDriver (30Hz心跳)
#        - WorldResolver (物理解算)
#        - GovernanceInterceptor (治理拦截)
#     8. 启动所有组件
#
# governance对应:
#   Runtime.boot_saga():
#     1. 初始化 GovernanceContext
#     2. 加载 Manifest (策略声明)
#     3. 启动 SagaOrchestrator
#     4. 注册 intervention_points
#
# 使用:
#   engine = WorldEngine()
#   engine.configure(tick_hz=30, max_individuals=1000)
#   engine.start()
#
#   # 注册个体
#   ind = engine.create_individual("agent_1", id=1)
#   ind.on_confirmed = lambda state: handle(state)
#   ind.request_move(delta_position=Vec3(1, 0, 0))
#
#   # 关闭
#   engine.shutdown()

import threading
from typing import Dict, List, Optional, Callable

from world.common.global_data import GlobalData, RunMode, ParameterServer, Parameter
from world.time.clock import Clock, ClockMode, Rate
from world.proto.channels import ChannelManager
from world.proto.world_types import Vec3, Quat
from world.service_discovery.topology import TopologyManager
from world.scheduler.scheduler import Scheduler
from world.component.component import ComponentManager
from world.component.world_resolver import WorldResolver
from world.component.tick_driver import WorldTickDriver
from world.governance.intervention import GovernanceInterceptor, TrustRing
from world.governance.event_log import EventLog
from world.node.node import Individual


class WorldEngine:
    """
    世界引擎 — 所有子系统的统一入口

    初始化顺序 (对应CyberRT mainboard.cc):
      1. GlobalData.instance() — 全局配置
      2. Clock.set_mode() — 时钟模式
      3. ChannelManager() — 通道管理
      4. TopologyManager.instance() — 拓扑管理
      5. Scheduler() — 协程调度器
      6. ParameterServer() — 运行时参数
      7. GovernanceInterceptor() — 治理层
      8. WorldResolver() — 物理解算
      9. WorldTickDriver() — 心跳驱动
    """

    def __init__(self):
        self._global_data = GlobalData.instance()
        self._channel_manager = ChannelManager()
        self._topology = TopologyManager.instance()
        self._scheduler: Optional[Scheduler] = None
        self._param_server = ParameterServer("world")
        self._component_manager = ComponentManager(self._channel_manager)
        self._event_log = EventLog()

        # 核心组件
        self._governance: Optional[GovernanceInterceptor] = None
        self._resolver: Optional[WorldResolver] = None
        self._tick_driver: Optional[WorldTickDriver] = None

        # 个体注册表
        self._individuals: Dict[int, Individual] = {}
        self._lock = threading.Lock()
        self._started = False

    def configure(self, *,
                  world_name: str = "default_world",
                  tick_hz: float = 30.0,
                  max_individuals: int = 10000,
                  cell_size: float = 2.0,
                  run_mode: RunMode = RunMode.REALITY,
                  governance_enabled: bool = True,
                  num_processors: int = 4):
        """
        配置世界参数

        可以在start()之前多次调用, 只有最后一次生效。
        """
        gd = self._global_data
        gd.world_name = world_name
        gd.tick_hz = tick_hz
        gd.cell_size = cell_size
        gd.run_mode = run_mode
        gd.governance_enabled = governance_enabled

        # 写入参数服务器
        self._param_server.set(Parameter("world/tick_hz", tick_hz))
        self._param_server.set(Parameter("world/max_individuals", max_individuals))
        self._param_server.set(Parameter("collision/cell_size", cell_size))
        self._param_server.set(Parameter("governance/enabled", governance_enabled))
        self._param_server.set(Parameter("scheduler/num_processors", num_processors))

    def start(self):
        """
        启动世界引擎

        CyberRT: cyber::Init() → mainboard.Run()
        governance: Runtime.boot()
        """
        if self._started:
            return

        gd = self._global_data

        # 1. 时钟模式
        if gd.run_mode == RunMode.SIMULATION:
            Clock.set_mode(ClockMode.MOCK)
        else:
            Clock.set_mode(ClockMode.CYBER)

        # 2. 调度器
        num_proc = self._param_server.get_or_default(
            "scheduler/num_processors", 4).as_int()
        self._scheduler = Scheduler(num_processors=num_proc)

        # 3. 治理层
        if gd.governance_enabled:
            self._governance = GovernanceInterceptor()

        # 4. WorldResolver — 物理解算
        self._resolver = WorldResolver(
            self._channel_manager,
            cell_size=gd.cell_size,
            governance_filter=self._governance.evaluate if self._governance else None,
        )

        # 5. WorldTickDriver — 心跳
        self._tick_driver = WorldTickDriver(
            self._channel_manager,
            tick_rate_hz=gd.tick_hz,
        )

        # 6. 启动tick
        self._tick_driver.start()
        self._started = True

    def create_individual(self, name: str, individual_id: int,
                          position: Vec3 = None,
                          trust_ring: TrustRing = TrustRing.RING_3_SANDBOX
                          ) -> Individual:
        """
        创建个体 — 注册到世界

        1. 创建Individual (封装Node + Writer/Reader)
        2. 注册到WorldResolver (初始位置)
        3. 注册到GovernanceInterceptor (信任环)
        """
        ind = Individual(name, individual_id, self._channel_manager)

        # 注册初始位置
        if position is None:
            position = Vec3()
        self._resolver.register_individual(individual_id, position)

        # 注册信任环
        if self._governance:
            self._governance.register_individual(individual_id, trust_ring)

        with self._lock:
            self._individuals[individual_id] = ind

        return ind

    def remove_individual(self, individual_id: int):
        """移除个体"""
        with self._lock:
            ind = self._individuals.pop(individual_id, None)
        if ind:
            ind.shutdown()

    def get_individual(self, individual_id: int) -> Optional[Individual]:
        with self._lock:
            return self._individuals.get(individual_id)

    @property
    def resolver(self) -> Optional[WorldResolver]:
        return self._resolver

    @property
    def tick_driver(self) -> Optional[WorldTickDriver]:
        return self._tick_driver

    @property
    def governance(self) -> Optional[GovernanceInterceptor]:
        return self._governance

    @property
    def event_log(self) -> EventLog:
        return self._event_log

    @property
    def param_server(self) -> ParameterServer:
        return self._param_server

    @property
    def channel_manager(self) -> ChannelManager:
        return self._channel_manager

    @property
    def topology(self) -> TopologyManager:
        return self._topology

    @property
    def individual_count(self) -> int:
        with self._lock:
            return len(self._individuals)

    def shutdown(self):
        """
        关闭世界引擎

        CyberRT: cyber::Shutdown()
        governance: Runtime.teardown()

        顺序: 先停tick → 停调度器 → 关闭个体 → 关闭拓扑
        """
        if not self._started:
            return

        # 1. 停止心跳
        if self._tick_driver:
            self._tick_driver.stop()

        # 2. 关闭所有个体
        with self._lock:
            for ind in self._individuals.values():
                ind.shutdown()
            self._individuals.clear()

        # 3. 关闭调度器
        if self._scheduler:
            self._scheduler.shutdown()

        # 4. 关闭拓扑
        self._topology.shutdown()

        # 5. 关闭组件管理器
        self._component_manager.shutdown_all()

        self._started = False
