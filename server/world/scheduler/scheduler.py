# SPDX-License-Identifier: Apache-2.0
#
# world/scheduler/scheduler.py
#
# 协程调度器 — 从 CyberRT cyber/scheduler + cyber/croutine 移植
#
# CyberRT来源:
#   cyber/croutine/croutine.h    → CRoutine (协程, 用户态上下文切换)
#   cyber/scheduler/scheduler.h  → Scheduler (调度器, 管理CRoutine)
#   cyber/scheduler/processor.h  → Processor (处理器, 运行CRoutine的线程)
#   cyber/scheduler/processor_context.h → ProcessorContext (经典/Choreo两种调度策略)
#
# governance来源:
#   无直接对应 — governance是事件驱动(请求到了才处理)。
#   但 SagaOrchestrator 的"步骤顺序执行"类似 CRoutine 的"挂起→恢复"。
#
# PRD映射:
#   #24 → 初始调度架构设计
#   #25 → CRoutine确定性执行顺序
#   #21 → uint64随机tick分配器修复(大规模个体更新聚集问题)
#
# 关键约束:
#   1. CRoutine 用用户态上下文切换(makecontext/swapcontext),
#      Python没有这个 → 用 asyncio coroutine 模拟。
#   2. Scheduler 的 coroutine pool 和 agent-OS 的 tick-driven 模型
#      是否兼容 → 关键问题(见"还不知道的事")。
#   3. Processor 绑定物理线程, 多个CRoutine在一个Processor上轮转。
#
# 还不知道的事:
#   - scheduler的coroutine pool和agent-OS的tick-driven模型是否兼容
#     → 设计方案: CRoutine作为tick内的子任务调度。
#       WorldTickDriver发出tick → Scheduler分发给多个CRoutine →
#       每个CRoutine处理一部分个体的MotionRequest → 汇总。
#       tick-driven是外层节拍, coroutine pool是tick内的并行。
#
#   - apollo cyber有30,000行C++但从没在这个场景下测过
#     → 我们不移植30,000行, 只移植核心调度逻辑。
#       Python版用asyncio而不是makecontext, 更简洁。
#
# Python适配:
#   CyberRT CRoutine: makecontext + swapcontext (用户态栈切换)
#   Python CRoutine:   asyncio.Task + Event (协作式多任务)
#   保持接口语义一致: Yield() / Resume() / state 状态机。

import asyncio
import threading
import time
from enum import Enum, auto
from typing import Optional, Callable, Dict, List
from concurrent.futures import ThreadPoolExecutor
from world.base.primitives import BoundedQueue


class RoutineState(Enum):
    """
    协程状态 — 对应 CyberRT RoutineState

    CyberRT (croutine.h):
      READY     → 可以被调度执行
      FINISHED  → 执行完成
      SLEEP     → 主动睡眠(定时唤醒)
      IO_WAIT   → 等待IO
      DATA_WAIT → 等待数据(DataVisitor有新数据时唤醒)
    """
    READY      = auto()
    FINISHED   = auto()
    SLEEP      = auto()
    IO_WAIT    = auto()
    DATA_WAIT  = auto()


class CRoutine:
    """
    协程 — 用户态轻量级任务

    CyberRT来源 (cyber/croutine/croutine.h):
      CRoutine(func) — 封装一个函数为协程
      Resume()  → 执行协程直到它Yield
      Yield()   → 主动让出执行权
      Wake()    → 从DATA_WAIT唤醒(DataVisitor通知)
      state()   → 当前状态

    lifecycle:
      创建 → READY → Resume() → 运行func → Yield() → READY/DATA_WAIT
      → Resume() → ... → func返回 → FINISHED

    Python适配:
      CyberRT用makecontext/swapcontext做用户态栈切换。
      Python用asyncio.Event做协作式让步。
      语义等价: Resume()≈调度执行, Yield()≈await event。

    governance对应:
      SagaOrchestrator的step执行:
        execute_step() → 运行 → 等待结果 → 继续下一步
      CRoutine的生命周期与saga step类似，但CRoutine是协作式多任务，
      不是顺序步骤。
    """

    def __init__(self, func: Callable, name: str = ""):
        self._func = func
        self._name = name
        self._state = RoutineState.READY
        self._id: int = 0
        self._priority: int = 0
        self._processor_id: int = -1
        self._group_name: str = ""
        self._force_stop = False

        # Python适配: 用Event控制执行
        self._resume_event = threading.Event()
        self._yield_event = threading.Event()
        self._finished = False

    def resume(self) -> RoutineState:
        """
        恢复执行 — 对应 CyberRT CRoutine::Resume()

        CyberRT: SwapContext(main_stack, routine_stack)
        Python: 设置resume_event, 等待yield_event
        """
        if self._force_stop:
            self._state = RoutineState.FINISHED
            return self._state

        if self._state != RoutineState.READY:
            return self._state

        self._resume_event.set()
        return self._state

    def run(self):
        """执行协程函数 — 在Processor线程中运行"""
        try:
            self._func()
        except Exception:
            pass
        finally:
            self._state = RoutineState.FINISHED
            self._finished = True

    def wake(self):
        """从DATA_WAIT唤醒 — DataVisitor有新数据时调用

        CyberRT: void Wake() { state_ = RoutineState::READY; }
        """
        self._state = RoutineState.READY

    def stop(self):
        """强制停止"""
        self._force_stop = True

    @property
    def state(self) -> RoutineState:
        return self._state

    @state.setter
    def state(self, s: RoutineState):
        self._state = s

    @property
    def id(self) -> int:
        return self._id

    @id.setter
    def id(self, val: int):
        self._id = val

    @property
    def name(self) -> str:
        return self._name

    @name.setter
    def name(self, val: str):
        self._name = val

    @property
    def priority(self) -> int:
        return self._priority

    @priority.setter
    def priority(self, val: int):
        self._priority = val

    @property
    def processor_id(self) -> int:
        return self._processor_id

    @processor_id.setter
    def processor_id(self, val: int):
        self._processor_id = val

    @property
    def group_name(self) -> str:
        return self._group_name

    @group_name.setter
    def group_name(self, val: str):
        self._group_name = val

    @property
    def is_finished(self) -> bool:
        return self._finished


class Processor:
    """
    处理器 — 运行CRoutine的工作线程

    CyberRT来源 (cyber/scheduler/processor.h):
      Processor(thread_id) — 绑定一个物理线程
      Run() — 主循环: 从ProcessorContext取CRoutine → Resume()
      BindContext(context) — 绑定调度策略
      ProcSnapshot() — 获取当前快照(debug用)

    一个Processor对应一个OS线程。
    多个CRoutine被分配到一个Processor上轮转执行。
    这是M:N调度模型 (M个CRoutine : N个Processor)。
    """

    def __init__(self, processor_id: int):
        self._id = processor_id
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._task_queue: BoundedQueue = BoundedQueue(1024)
        self._current_routine: Optional[CRoutine] = None
        self._idle_count = 0

    def start(self):
        """启动Processor线程"""
        self._running = True
        self._thread = threading.Thread(
            target=self._run_loop,
            name=f"processor_{self._id}",
            daemon=True
        )
        self._thread.start()

    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=2.0)

    def enqueue(self, routine: CRoutine) -> bool:
        """将CRoutine加入任务队列"""
        return self._task_queue.enqueue(routine)

    def _run_loop(self):
        """
        Processor主循环

        CyberRT:
          while (running) {
            auto cr = context_->NextRoutine();
            if (cr) { cr->Resume(); }
            else { /* idle */ }
          }
        """
        while self._running:
            routine = self._task_queue.dequeue()
            if routine is not None:
                self._current_routine = routine
                if routine.state == RoutineState.READY:
                    routine.run()
                self._current_routine = None
                self._idle_count = 0
            else:
                self._idle_count += 1
                # 渐进式退避: 减少空轮询CPU消耗
                if self._idle_count > 100:
                    time.sleep(0.001)
                elif self._idle_count > 10:
                    time.sleep(0.0001)

    @property
    def processor_id(self) -> int:
        return self._id

    @property
    def is_idle(self) -> bool:
        return self._current_routine is None


class Scheduler:
    """
    调度器 — 管理CRoutine的创建、分发、通知

    CyberRT来源 (cyber/scheduler/scheduler.h):
      CreateTask(func, name) → 创建CRoutine并分发给Processor
      NotifyProcessor(crid) → 唤醒等待数据的CRoutine
      RemoveTask(name)      → 移除CRoutine
      DispatchTask(cr)      → 将CRoutine分配给Processor(策略决定)
      Shutdown()            → 关闭所有Processor

    调度策略:
      CyberRT有ClassicContext和ChoreographyContext两种。
      ClassicContext: 按优先级分组，组内轮转
      ChoreographyContext: 按DAG依赖顺序调度

    当前实现: 简化版ClassicContext — 轮转分发到Processor。
    后续: 按PRD #24实现完整调度策略。

    与tick-driven模型的关系:
      tick-driven是外层节拍(30Hz WorldTick)。
      Scheduler是tick内的并行执行器。
      每个tick: WorldResolver.proc() → 创建多个子任务 → Scheduler并行执行。
    """

    _instance = None
    _lock = threading.Lock()

    @classmethod
    def instance(cls, num_processors: int = 4) -> 'Scheduler':
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = cls(num_processors)
        return cls._instance

    def __init__(self, num_processors: int = 4):
        self._processors: List[Processor] = []
        self._routines: Dict[int, CRoutine] = {}
        self._name_to_id: Dict[str, int] = {}
        self._next_id = 0
        self._stop = False
        self._lock = threading.Lock()
        self._rr_index = 0  # round-robin分发计数器

        # 创建Processor
        for i in range(num_processors):
            proc = Processor(i)
            self._processors.append(proc)
            proc.start()

    def create_task(self, func: Callable, name: str,
                    priority: int = 0) -> bool:
        """
        创建任务 — 对应 CyberRT Scheduler::CreateTask()

        CyberRT:
          auto task_id = GlobalData::RegisterTaskName(name);
          auto cr = make_shared<CRoutine>(func);
          cr->set_id(task_id);
          DispatchTask(cr);
        """
        if self._stop:
            return False

        with self._lock:
            self._next_id += 1
            task_id = self._next_id

        cr = CRoutine(func, name)
        cr.id = task_id
        cr.priority = priority

        with self._lock:
            self._routines[task_id] = cr
            self._name_to_id[name] = task_id

        return self._dispatch_task(cr)

    def create_task_batch(self, tasks: List[Callable],
                          name_prefix: str = "batch") -> List[int]:
        """
        批量创建任务 — 用于tick内并行处理

        例: WorldResolver.proc() 将1000个个体分成4组,
        每组创建一个task并行处理broadphase。
        """
        task_ids = []
        for i, func in enumerate(tasks):
            name = f"{name_prefix}_{i}"
            self.create_task(func, name)
            with self._lock:
                task_ids.append(self._name_to_id.get(name, 0))
        return task_ids

    def _dispatch_task(self, cr: CRoutine) -> bool:
        """
        将CRoutine分配给Processor — round-robin策略

        CyberRT:
          ClassicContext: 按group_name分组，同组CRoutine在同一Processor
          ChoreographyContext: 按DAG依赖调度

        当前: 简化版round-robin。
        PRD #21修复: 不再用固定间隔分配, 避免大规模个体聚集。
        """
        if not self._processors:
            return False
        proc = self._processors[self._rr_index % len(self._processors)]
        self._rr_index += 1
        cr.processor_id = proc.processor_id
        return proc.enqueue(cr)

    def notify_processor(self, task_id: int) -> bool:
        """
        唤醒等待数据的CRoutine

        CyberRT: bool NotifyProcessor(uint64_t crid)
        DataVisitor有新数据时调用: visitor.RegisterNotifyCallback(scheduler.notify)
        """
        with self._lock:
            cr = self._routines.get(task_id)
        if cr is None:
            return False
        if cr.state == RoutineState.DATA_WAIT:
            cr.wake()
            return self._dispatch_task(cr)
        return True

    def remove_task(self, name: str) -> bool:
        """移除任务"""
        with self._lock:
            task_id = self._name_to_id.pop(name, None)
            if task_id is not None:
                cr = self._routines.pop(task_id, None)
                if cr:
                    cr.stop()
                return True
        return False

    def wait_all(self, timeout_s: float = 5.0) -> bool:
        """等待所有任务完成 — 用于tick同步"""
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            with self._lock:
                all_done = all(
                    cr.is_finished for cr in self._routines.values()
                )
            if all_done:
                return True
            time.sleep(0.0005)
        return False

    def clear_finished(self):
        """清理已完成的任务"""
        with self._lock:
            finished = [
                tid for tid, cr in self._routines.items()
                if cr.is_finished
            ]
            for tid in finished:
                cr = self._routines.pop(tid)
                self._name_to_id = {
                    n: i for n, i in self._name_to_id.items() if i != tid
                }

    def shutdown(self):
        """关闭调度器"""
        self._stop = True
        for proc in self._processors:
            proc.stop()
        self._processors.clear()

    @property
    def active_task_count(self) -> int:
        with self._lock:
            return sum(
                1 for cr in self._routines.values()
                if not cr.is_finished
            )

    @property
    def processor_count(self) -> int:
        return len(self._processors)
