# SPDX-License-Identifier: Apache-2.0
#
# world/component/component.py
#
# Component 框架 — 从 CyberRT cyber/component 移植
#
# CyberRT来源:
#   cyber/component/component_base.h → ComponentBase (组件基类)
#   cyber/component/component.h      → Component<M0..M3> (模板组件)
#   cyber/component/timer_component.h → TimerComponent (定时器组件)
#
# 语义:
#   Component = 世界中的一个处理单元。
#   每个Component有一个Node, 通过Node订阅消息, 触发Proc()处理。
#
#   CyberRT的Component是模板类, 支持1-4个消息输入:
#     Component<M0> → Proc(msg0)
#     Component<M0, M1> → Proc(msg0, msg1)
#
#   pubsub-loop简化:
#     Component(channels) → proc(messages)  # 统一用可变参数
#
# 现有组件(已经存在):
#   WorldResolver    — 物理解算组件 (收集→碰撞→修正→提交)
#   WorldTickDriver  — 世界心跳组件 (30Hz tick)
#   LegacyAdapter    — 旧API适配器 (set_position → MotionRequest)
#   GovernanceInterceptor — 治理拦截器 (信任环→准入)
#
# 这个文件定义了通用Component基类, 让上述组件可以统一管理。
#
# governance对应:
#   Component ≈ Saga step executor
#   Init() ≈ saga step registration
#   Proc() ≈ saga step execution
#   Shutdown() ≈ compensation callback

import threading
from typing import Any, Callable, Dict, List, Optional
from world.node.node import Node
from world.proto.channels import ChannelManager


class ComponentBase:
    """
    组件基类 — 所有世界组件的公共接口

    CyberRT来源 (component_base.h):
      Init() → 纯虚方法, 子类实现初始化
      Shutdown() → 关闭组件, 移除任务
      node_ → 组件的Node (通信入口)
      readers_ → 订阅的Reader列表

    pubsub-loop的每个组件:
      1. 创建时分配一个Node (向拓扑管理器注册)
      2. Init()中创建Writer/Reader
      3. 当消息到达时, 框架调用Proc()
      4. Shutdown()释放资源
    """

    def __init__(self, name: str, channel_manager: ChannelManager):
        self._name = name
        self._node: Optional[Node] = None
        self._channel_manager = channel_manager
        self._is_shutdown = False

    def initialize(self, individual_id: int = 0) -> bool:
        """
        初始化组件 — 创建Node + 调用用户Init()

        CyberRT:
          1. 创建Node
          2. 加载配置
          3. 调用Init()
          4. 创建Reader → 绑定到Scheduler任务
        """
        self._node = Node(self._name, individual_id, self._channel_manager)
        return self.init()

    def init(self) -> bool:
        """
        子类实现 — 初始化逻辑

        CyberRT: virtual bool Init() = 0
        在这里创建Writer/Reader, 初始化状态。
        """
        raise NotImplementedError

    def proc(self, *messages) -> bool:
        """
        子类实现 — 处理消息

        CyberRT: virtual bool Proc(msg0, ...) = 0
        当订阅的消息到达时, 框架调用此方法。
        返回True表示处理成功。
        """
        raise NotImplementedError

    def shutdown(self):
        """
        关闭组件

        CyberRT: virtual void Shutdown()
        """
        if self._is_shutdown:
            return
        self._is_shutdown = True
        if self._node:
            self._node.shutdown()

    @property
    def node(self) -> Optional[Node]:
        return self._node

    @property
    def name(self) -> str:
        return self._name

    @property
    def is_shutdown(self) -> bool:
        return self._is_shutdown


class Component(ComponentBase):
    """
    消息驱动组件 — 当消息到达时触发Proc()

    CyberRT来源 (component.h):
      Component<M0> — 单消息输入
      Component<M0, M1> — 双消息输入(DataVisitor AllLatest融合)

    使用:
      class MyComponent(Component):
          def init(self):
              self.writer = self.node.create_writer("/output", OutputType)
              return True

          def proc(self, msg):
              result = process(msg)
              self.writer.write(result)
              return True

      comp = MyComponent("my_comp", cm)
      comp.initialize()
      comp.subscribe("/input", InputType)  # 消息到达时自动调用proc
    """

    def __init__(self, name: str, channel_manager: ChannelManager):
        super().__init__(name, channel_manager)
        self._input_channels: List[str] = []

    def subscribe(self, channel_name: str, msg_type: type):
        """
        订阅消息通道 — 消息到达时自动调用proc()

        CyberRT: Initialize()中创建Reader, 绑定到Scheduler
        """
        self._input_channels.append(channel_name)
        self._node.create_reader(
            channel_name, msg_type,
            callback=lambda msg: self._on_message(msg)
        )

    def _on_message(self, msg):
        if not self._is_shutdown:
            self.proc(msg)


class TimerComponent(ComponentBase):
    """
    定时器组件 — 按固定频率触发Proc()

    CyberRT来源 (timer_component.h):
      TimerComponent — 不订阅消息, 而是按固定间隔执行Proc()
      interval_ → 定时器间隔(ms)

    与Component的区别:
      Component: 消息驱动 → 有消息才执行
      TimerComponent: 时间驱动 → 固定频率执行

    pubsub-loop使用:
      WorldTickDriver 本质上就是一个 TimerComponent(interval=33ms)。
      统计输出组件也是 TimerComponent(interval=1000ms)。
    """

    def __init__(self, name: str, channel_manager: ChannelManager,
                 interval_ms: float = 33.33):
        super().__init__(name, channel_manager)
        self._interval_ms = interval_ms
        self._timer = None
        self._running = False
        self._thread: Optional[threading.Thread] = None

    def initialize(self, individual_id: int = 0) -> bool:
        ok = super().initialize(individual_id)
        if ok:
            self._start_timer()
        return ok

    def _start_timer(self):
        """启动定时器循环"""
        from world.time.clock import Rate, Clock
        self._running = True
        freq = 1000.0 / self._interval_ms
        self._thread = threading.Thread(
            target=self._timer_loop,
            args=(freq,),
            daemon=True
        )
        self._thread.start()

    def _timer_loop(self, freq_hz: float):
        from world.time.clock import Rate
        rate = Rate(freq_hz)
        while self._running:
            if not self._is_shutdown:
                self.proc()
            rate.sleep()

    def shutdown(self):
        self._running = False
        super().shutdown()
        if self._thread:
            self._thread.join(timeout=1.0)


class ComponentManager:
    """
    组件管理器 — 管理所有组件的生命周期

    CyberRT来源:
      mainboard/module_controller.h → 加载/卸载组件
      对应 ComponentBase 的注册/销毁

    pubsub-loop: 管理WorldResolver, TickDriver等组件。
    """

    def __init__(self, channel_manager: ChannelManager):
        self._channel_manager = channel_manager
        self._components: Dict[str, ComponentBase] = {}
        self._lock = threading.Lock()

    def register(self, component: ComponentBase) -> bool:
        """注册组件"""
        with self._lock:
            if component.name in self._components:
                return False
            self._components[component.name] = component
        return True

    def get(self, name: str) -> Optional[ComponentBase]:
        with self._lock:
            return self._components.get(name)

    def all_components(self) -> List[ComponentBase]:
        with self._lock:
            return list(self._components.values())

    def shutdown_all(self):
        """关闭所有组件"""
        with self._lock:
            for comp in self._components.values():
                comp.shutdown()
            self._components.clear()
