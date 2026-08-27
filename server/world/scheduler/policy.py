# SPDX-License-Identifier: Apache-2.0
#
# world/scheduler/policy.py
#
# 调度策略 — 从 CyberRT cyber/scheduler/policy 移植
#
# CyberRT来源:
#   scheduler/policy/classic_context.h/cc       → ClassicContext
#   scheduler/policy/choreography_context.h/cc  → ChoreographyContext
#   scheduler/policy/scheduler_classic.cc       → SchedulerClassic
#   scheduler/policy/scheduler_choreography.cc  → SchedulerChoreography
#
# governance来源:
#   SagaOrchestrator — 步骤依赖的顺序调度 (类似 Choreography)
#   ExecutionRing — 信任等级影响调度优先级
#
# PRD映射:
#   #24 → 初始调度架构设计 (ClassicContext + ChoreographyContext)
#   #25 → 确定性执行顺序 (run-to-run deterministic)
#   #21 → 随机tick分配器修复 (避免4096倍数聚集)
#
# 关键约束:
#   1. ClassicContext: 按组分优先级轮转 — 默认策略
#   2. ChoreographyContext: 按DAG依赖顺序 — 用于有严格顺序要求的pipeline
#   3. 确定性: 相同输入 → 相同调度顺序 (用于仿真回放)
#   4. 与 tick-driven 的关系:
#      tick-driven 是外层节拍(30Hz)
#      调度策略是 tick 内的子任务排序

import threading
from abc import ABC, abstractmethod
from collections import defaultdict
from typing import Optional, Callable, Dict, List, Set, Tuple

from world.scheduler.scheduler import CRoutine, RoutineState


class ProcessorContext(ABC):
    """
    处理器上下文基类 — 决定 Processor 下一个执行哪个 CRoutine

    CyberRT (processor_context.h):
      ProcessorContext(group_name)
      NextRoutine() → CRoutine*  (核心: 返回下一个应该执行的协程)
      Wait() → 无任务时等待唤醒
      Shutdown()
    """

    @abstractmethod
    def next_routine(self) -> Optional[CRoutine]:
        """返回下一个应该执行的CRoutine"""
        ...

    @abstractmethod
    def add_routine(self, routine: CRoutine):
        """添加CRoutine到调度队列"""
        ...

    @abstractmethod
    def remove_routine(self, routine_id: int) -> bool:
        """移除CRoutine"""
        ...

    @abstractmethod
    def notify(self):
        """唤醒 — 有新任务或新数据时调用"""
        ...


class ClassicContext(ProcessorContext):
    """
    经典调度上下文 — 按优先级分组, 组内轮转

    CyberRT来源 (classic_context.h/cc):
      数据结构:
        cr_group_: Map<group_name, Map<priority, List<CRoutine>>>
        多级优先级队列, 同优先级内轮转

      NextRoutine():
        for each priority level (high → low):
          for each routine in this priority:
            if routine.state == READY: return routine
        return nullptr (无可调度任务)

    PRD #25 确定性:
      CyberRT 的 NextRoutine() 按优先级+插入顺序遍历,
      确定性取决于 CRoutine 的创建顺序。
      pubsub-loop 保持相同语义: 同一 tick 内, 相同的 create_task 调用顺序
      → 相同的调度顺序 → 确定性输出。

    governance对应:
      ExecutionRing 的优先级映射:
        RING_0_KERNEL → priority 0 (最高)
        RING_1_SUPERVISOR → priority 1
        RING_2_USER → priority 2
        RING_3_SANDBOX → priority 3
      高信任个体(Ring 0)的任务优先调度。
    """

    def __init__(self, group_name: str = "default"):
        self._group_name = group_name
        # priority → [CRoutine]  (低数字 = 高优先级)
        self._priority_queues: Dict[int, List[CRoutine]] = defaultdict(list)
        self._lock = threading.Lock()
        self._notify_event = threading.Event()

    def add_routine(self, routine: CRoutine):
        """
        添加CRoutine到对应优先级队列

        CyberRT: ClassicContext::Enqueue(cr)
          auto& croutines = cr_group_[grp].at(prio);
          croutines.emplace_back(cr);
        """
        with self._lock:
            prio = routine.priority
            self._priority_queues[prio].append(routine)
        self.notify()

    def remove_routine(self, routine_id: int) -> bool:
        with self._lock:
            for prio, routines in self._priority_queues.items():
                for i, cr in enumerate(routines):
                    if cr.id == routine_id:
                        routines.pop(i)
                        return True
        return False

    def next_routine(self) -> Optional[CRoutine]:
        """
        按优先级遍历, 返回第一个READY的CRoutine

        CyberRT: ClassicContext::NextRoutine()
          for (int i = MAX_PRIO - 1; i >= 0; --i) {
            for (auto& cr : multi_pri_rq_[i]) {
              if (cr->state() == READY) return cr;
            }
          }
        """
        with self._lock:
            # 按优先级排序(低数字优先)
            for prio in sorted(self._priority_queues.keys()):
                routines = self._priority_queues[prio]
                for cr in routines:
                    if cr.state == RoutineState.READY:
                        return cr
        return None

    def notify(self):
        """唤醒等待的Processor — CyberRT: ClassicContext::Notify()"""
        self._notify_event.set()

    def wait(self, timeout_s: float = 0.1):
        """等待唤醒 — Processor空闲时调用"""
        self._notify_event.wait(timeout=timeout_s)
        self._notify_event.clear()

    @property
    def routine_count(self) -> int:
        with self._lock:
            return sum(len(q) for q in self._priority_queues.values())


class ChoreographyContext(ProcessorContext):
    """
    编排调度上下文 — 按DAG依赖顺序调度

    CyberRT来源 (choreography_context.h/cc):
      按明确的顺序执行CRoutine (不是优先级, 而是DAG依赖)。
      用于有严格先后顺序的pipeline:
        A → B → C  (A完成后才能运行B, B完成后才能运行C)

    pubsub-loop使用场景:
      WorldResolver 的三阶段pipeline:
        Phase 1: collect_requests  (收集MotionRequest)
        Phase 2: broadphase + narrowphase  (碰撞检测)
        Phase 3: resolve + publish  (解算 + 发布ConfirmedState)
      三个阶段必须严格顺序执行。

    governance对应:
      SagaOrchestrator: create_saga → add_step → execute_step
      步骤顺序执行, 一个步骤失败可以回滚。
      ChoreographyContext 类似但没有回滚(物理引擎不需要回滚)。

    确定性保证:
      DAG顺序由创建时指定, 不依赖运行时状态。
      相同的DAG → 相同的执行顺序 → 确定性。
    """

    def __init__(self, group_name: str = "choreography"):
        self._group_name = group_name
        self._order: List[CRoutine] = []
        self._current_index = 0
        self._lock = threading.Lock()
        self._notify_event = threading.Event()

    def add_routine(self, routine: CRoutine):
        """按插入顺序添加 — 插入顺序即执行顺序"""
        with self._lock:
            self._order.append(routine)
        self.notify()

    def remove_routine(self, routine_id: int) -> bool:
        with self._lock:
            for i, cr in enumerate(self._order):
                if cr.id == routine_id:
                    self._order.pop(i)
                    if i < self._current_index:
                        self._current_index -= 1
                    return True
        return False

    def next_routine(self) -> Optional[CRoutine]:
        """
        按编排顺序返回下一个READY的CRoutine

        与ClassicContext不同: 严格按 _order 列表的顺序执行,
        当前CRoutine完成后才能执行下一个。
        """
        with self._lock:
            while self._current_index < len(self._order):
                cr = self._order[self._current_index]
                if cr.is_finished:
                    self._current_index += 1
                    continue
                if cr.state == RoutineState.READY:
                    return cr
                break  # 当前未完成且不READY — 等待
        return None

    def reset(self):
        """重置执行指针 — 下一个tick重新开始编排"""
        with self._lock:
            self._current_index = 0

    def notify(self):
        self._notify_event.set()

    def wait(self, timeout_s: float = 0.1):
        self._notify_event.wait(timeout=timeout_s)
        self._notify_event.clear()

    @property
    def routine_count(self) -> int:
        with self._lock:
            return len(self._order)


# ─── Tick Assignment — PRD #21 修复 ───
#
# 问题:
#   CyberRT 的随机tick分配用 uint64 粒度, 但实现中只产生 4096 倍数的间隔。
#   当有1000个个体时, 所有个体的更新tick聚集在固定的4096边界上,
#   导致某些tick有大量个体同时更新(突发峰值), 其他tick空闲。
#
# 解决:
#   使用 golden-ratio 散列(Fibonacci hashing) 替代 random。
#   优势: 均匀分散, 确定性, 无碰撞。
#
# CyberRT: 无此优化。CyberRT的场景没有1000个独立调度的个体。
# pubsub-loop: "世界中有1000个个体" — 需要解决此问题。

# 黄金比例哈希常数 (2^64 / φ)
GOLDEN_RATIO_64 = 0x9E3779B97F4A7C15


def assign_tick_offset(individual_id: int, total_ticks: int) -> int:
    """
    为个体分配tick偏移量 — 使用 Fibonacci hashing

    PRD #21: 修复4096倍数聚集问题

    确保1000个个体均匀分散到 total_ticks 个时间片上。
    确定性: 相同individual_id → 相同偏移量。

    >>> offsets = [assign_tick_offset(i, 30) for i in range(100)]
    >>> len(set(offsets))  # 应该接近30 (均匀分散到30个slot)
    30
    """
    h = (individual_id * GOLDEN_RATIO_64) & 0xFFFFFFFFFFFFFFFF
    return h % total_ticks


def create_tick_schedule(individual_ids: List[int],
                         ticks_per_cycle: int = 30
                         ) -> Dict[int, List[int]]:
    """
    为一批个体创建tick调度表

    返回 {tick_offset: [individual_ids]}
    个体分散到不同的tick上, 避免峰值。

    1000个个体, 30个tick → 每个tick约33个个体 (均匀)。
    """
    schedule: Dict[int, List[int]] = defaultdict(list)
    for iid in individual_ids:
        offset = assign_tick_offset(iid, ticks_per_cycle)
        schedule[offset].append(iid)
    return dict(schedule)


# ─── Deterministic Ordering — PRD #25 ───

class DeterministicTaskOrder:
    """
    确定性任务排序 — 保证 run-to-run 相同的执行顺序

    PRD #25: CRoutine 的确定性执行顺序

    用途:
      仿真回放(replay): 相同的初始状态 + 相同的输入 → 相同的输出。
      需要调度器保证相同的执行顺序。

    实现:
      按 (priority, creation_order) 排序。
      creation_order 由 tick_seq * MAX_TASKS + task_index 计算,
      保证全局唯一且确定。
    """
    MAX_TASKS_PER_TICK = 10000

    def __init__(self):
        self._creation_counter = 0
        self._lock = threading.Lock()

    def assign_order(self, routine: CRoutine, tick_seq: int):
        """分配确定性执行序号"""
        with self._lock:
            order = tick_seq * self.MAX_TASKS_PER_TICK + self._creation_counter
            self._creation_counter += 1
        routine.id = order

    def sort_key(self, routine: CRoutine) -> Tuple[int, int]:
        """排序键: (priority, creation_order)"""
        return (routine.priority, routine.id)

    def ordered(self, routines: List[CRoutine]) -> List[CRoutine]:
        """按确定性顺序排列"""
        return sorted(routines, key=self.sort_key)

    def reset_tick(self):
        """每个tick开始时重置计数器"""
        with self._lock:
            self._creation_counter = 0
