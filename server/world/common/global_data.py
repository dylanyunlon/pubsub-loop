# SPDX-License-Identifier: Apache-2.0
#
# world/common/global_data.py
#
# 全局数据 + 参数服务 — 从 CyberRT cyber/common + cyber/parameter 移植
#
# CyberRT来源:
#   cyber/common/global_data.h → GlobalData (全局运行时数据: 主机名, PID, 配置)
#   cyber/parameter/parameter.h → Parameter (类型安全的键值参数)
#   cyber/parameter/parameter_server.h → ParameterServer (参数服务端)
#   cyber/parameter/parameter_client.h → ParameterClient (参数客户端)
#
# 语义:
#   GlobalData: 世界运行时的全局信息(单例)
#     - 世界名称、tick频率、最大个体数
#     - 进程信息(PID, host)
#     - 运行模式(实时/仿真)
#
#   Parameter: 运行时可调参数
#     - 碰撞检测cell_size
#     - 治理信任环配置
#     - 物理参数(摩擦、重力)
#     → 不需要重启世界就能调整
#
# governance对应:
#   GlobalData ≈ GovernanceContext (全局会话上下文)
#   ParameterServer ≈ PolicyStore (策略存储)

import os
import threading
import time
from enum import Enum, auto
from typing import Any, Callable, Dict, List, Optional, Union


class RunMode(Enum):
    """运行模式"""
    REALITY = auto()   # 实时模式 — 与系统时钟同步
    SIMULATION = auto() # 仿真模式 — 手动控制时间


class GlobalData:
    """
    全局数据 — 世界运行时的全局单例

    CyberRT来源 (global_data.h):
      ProcessId() → int
      HostName() → string
      ProcessGroup() → string (组件组名)
      config_ → CyberConfig (框架配置)

    pubsub-loop扩展:
      world_name → 世界实例名称
      tick_hz → 世界tick频率 (default: 30)
      max_individuals → 最大个体数 (default: 10000)
      run_mode → REALITY / SIMULATION
    """

    _instance = None
    _lock = threading.Lock()

    @classmethod
    def instance(cls) -> 'GlobalData':
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = cls()
        return cls._instance

    def __init__(self):
        # 进程信息 — CyberRT: ProcessId(), HostName()
        self._process_id = os.getpid()
        self._host_name = os.uname().nodename
        self._process_group = "world_default"

        # 世界配置
        self._world_name = "default_world"
        self._tick_hz: float = 30.0
        self._max_individuals: int = 10000
        self._run_mode = RunMode.REALITY
        self._start_time_ns = time.monotonic_ns()

        # 碰撞检测配置
        self._cell_size: float = 2.0
        self._broadphase_budget_ms: float = 3.0

        # 治理配置
        self._governance_enabled: bool = True
        self._default_trust_ring: int = 3  # RING_3 = 外部agent

    @property
    def process_id(self) -> int:
        return self._process_id

    @property
    def host_name(self) -> str:
        return self._host_name

    @property
    def process_group(self) -> str:
        return self._process_group

    @process_group.setter
    def process_group(self, value: str):
        self._process_group = value

    @property
    def world_name(self) -> str:
        return self._world_name

    @world_name.setter
    def world_name(self, value: str):
        self._world_name = value

    @property
    def tick_hz(self) -> float:
        return self._tick_hz

    @tick_hz.setter
    def tick_hz(self, value: float):
        self._tick_hz = value

    @property
    def max_individuals(self) -> int:
        return self._max_individuals

    @property
    def run_mode(self) -> RunMode:
        return self._run_mode

    @run_mode.setter
    def run_mode(self, value: RunMode):
        self._run_mode = value

    @property
    def cell_size(self) -> float:
        return self._cell_size

    @cell_size.setter
    def cell_size(self, value: float):
        self._cell_size = value

    @property
    def governance_enabled(self) -> bool:
        return self._governance_enabled

    @governance_enabled.setter
    def governance_enabled(self, value: bool):
        self._governance_enabled = value

    def uptime_sec(self) -> float:
        return (time.monotonic_ns() - self._start_time_ns) / 1_000_000_000


# ── Parameter Service ────────────────────────────────────
#
# CyberRT来源:
#   cyber/parameter/parameter.h
#     Parameter(name, value) — bool/int/float/string/bytes
#     Type() → ParamType
#     Name() → string
#     AsXxx() → typed value
#
#   cyber/parameter/parameter_server.h
#     SetParameter(param)
#     GetParameter(name, param) → bool
#     ListParameters(params)
#
#   cyber/parameter/parameter_client.h
#     SetParameter(param) → 通过service请求到server
#     GetParameter(name) → Parameter
#
# pubsub-loop简化:
#   Python不需要模板特化, 直接用dict存储。
#   参数变更通过Signal通知。

class ParamType(Enum):
    """参数类型"""
    BOOL   = auto()
    INT    = auto()
    FLOAT  = auto()
    STRING = auto()


class Parameter:
    """
    类型安全参数

    CyberRT来源 (parameter.h):
      Parameter(name, bool_value)
      Parameter(name, int_value)
      Parameter(name, double_value)
      Parameter(name, string_value)
    """

    def __init__(self, name: str, value: Union[bool, int, float, str]):
        self._name = name
        self._value = value

        if isinstance(value, bool):
            self._type = ParamType.BOOL
        elif isinstance(value, int):
            self._type = ParamType.INT
        elif isinstance(value, float):
            self._type = ParamType.FLOAT
        elif isinstance(value, str):
            self._type = ParamType.STRING
        else:
            raise TypeError(f"Unsupported parameter type: {type(value)}")

    @property
    def name(self) -> str:
        return self._name

    @property
    def value(self) -> Any:
        return self._value

    @property
    def type(self) -> ParamType:
        return self._type

    def as_bool(self) -> bool:
        return bool(self._value)

    def as_int(self) -> int:
        return int(self._value)

    def as_float(self) -> float:
        return float(self._value)

    def as_string(self) -> str:
        return str(self._value)

    def __repr__(self):
        return f"Parameter({self._name!r}, {self._value!r})"


class ParameterServer:
    """
    参数服务器 — 集中管理运行时参数

    CyberRT来源 (parameter_server.h):
      SetParameter(param) → 设置参数
      GetParameter(name) → 获取参数
      ListParameters() → 列出所有参数

    使用:
      server = ParameterServer("world_params")

      # 设置碰撞检测参数
      server.set(Parameter("collision/cell_size", 2.0))
      server.set(Parameter("collision/budget_ms", 3.0))

      # 运行时调整(不需要重启)
      server.set(Parameter("collision/cell_size", 5.0))

      # 获取
      cell = server.get("collision/cell_size")
      print(cell.as_float())  # 5.0

    governance对应:
      ParameterServer ≈ PolicyStore
      Parameter("trust/ring_0_priority", 100.0) ≈ policy rule
    """

    def __init__(self, name: str = "default"):
        self._name = name
        self._params: Dict[str, Parameter] = {}
        self._callbacks: Dict[str, List[Callable]] = {}
        self._lock = threading.Lock()

    def set(self, param: Parameter):
        """设置参数 — CyberRT: SetParameter"""
        with self._lock:
            old = self._params.get(param.name)
            self._params[param.name] = param
            cbs = list(self._callbacks.get(param.name, []))

        # 通知变更回调(锁外)
        for cb in cbs:
            try:
                cb(param, old)
            except Exception:
                pass

    def get(self, name: str) -> Optional[Parameter]:
        """获取参数 — CyberRT: GetParameter"""
        with self._lock:
            return self._params.get(name)

    def get_or_default(self, name: str,
                       default: Union[bool, int, float, str]) -> Parameter:
        """获取参数, 不存在则返回默认值"""
        with self._lock:
            param = self._params.get(name)
        if param is not None:
            return param
        return Parameter(name, default)

    def list_params(self) -> List[Parameter]:
        """列出所有参数 — CyberRT: ListParameters"""
        with self._lock:
            return list(self._params.values())

    def on_change(self, name: str,
                  callback: Callable[[Parameter, Optional[Parameter]], None]):
        """注册参数变更回调"""
        with self._lock:
            if name not in self._callbacks:
                self._callbacks[name] = []
            self._callbacks[name].append(callback)

    @property
    def name(self) -> str:
        return self._name
