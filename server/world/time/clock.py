# SPDX-License-Identifier: Apache-2.0
#
# world/time/clock.py
#
# 世界时钟与定时器 — 从 CyberRT cyber/time + cyber/timer 移植
#
# CyberRT来源:
#   cyber/time/clock.h → Clock (时钟, CYBER/MOCK两种模式)
#   cyber/time/time.h  → Time (时间值类型, 纳秒精度)
#   cyber/time/rate.h  → Rate (频率控制器, 用于固定频率循环)
#   cyber/time/duration.h → Duration (时间间隔)
#   cyber/timer/timer.h → Timer (周期/单次定时器)
#   cyber/timer/timing_wheel.h → TimingWheel (时间轮, 高效定时器管理)
#
# 语义:
#   世界时钟有两种模式:
#   1. CYBER模式(默认): 使用系统时钟, 实时推进
#   2. MOCK模式: 手动控制时间推进, 用于确定性仿真
#
#   WorldTickDriver 使用 Clock.now() 获取当前时间,
#   Rate 控制固定30Hz频率。
#   Timer 用于周期性任务(如垃圾回收、统计输出)。
#   TimingWheel 是Timer的底层实现。
#
# governance对应:
#   SagaOrchestrator 中的 timeout 机制
#   Session TTL 管理

import time
import threading
from dataclasses import dataclass
from enum import Enum, auto
from typing import Callable, Optional, List


class ClockMode(Enum):
    """时钟模式"""
    CYBER = auto()  # 使用系统时钟 (time.monotonic_ns)
    MOCK  = auto()  # 手动控制时间 (确定性仿真)


class Duration:
    """
    时间间隔 — 纳秒精度

    CyberRT来源 (duration.h):
      Duration(nanoseconds)
      ToSecond() → double
      ToNanosecond() → int64
    """
    __slots__ = ('_ns',)

    def __init__(self, nanoseconds: int = 0):
        self._ns = nanoseconds

    @classmethod
    def from_seconds(cls, seconds: float) -> 'Duration':
        return cls(int(seconds * 1_000_000_000))

    @classmethod
    def from_millis(cls, millis: float) -> 'Duration':
        return cls(int(millis * 1_000_000))

    @property
    def nanoseconds(self) -> int:
        return self._ns

    def to_sec(self) -> float:
        return self._ns / 1_000_000_000

    def to_ms(self) -> float:
        return self._ns / 1_000_000

    def __add__(self, other: 'Duration') -> 'Duration':
        return Duration(self._ns + other._ns)

    def __sub__(self, other: 'Duration') -> 'Duration':
        return Duration(self._ns - other._ns)

    def __lt__(self, other: 'Duration') -> bool:
        return self._ns < other._ns

    def __le__(self, other: 'Duration') -> bool:
        return self._ns <= other._ns

    def __repr__(self):
        return f"Duration({self.to_ms():.3f}ms)"


class Clock:
    """
    世界时钟 — 单例

    CyberRT来源 (clock.h):
      Clock::Now() → Time
      Clock::SetMode(ClockMode) → 切换时钟模式
      Clock::SetNow(Time) → MOCK模式手动设时间

    CYBER模式: time.monotonic_ns() — 单调时钟, 不受NTP影响
    MOCK模式: 手动推进, 每个tick_driver.tick()调用set_now()

    为什么用单调时钟:
      世界仿真需要"经过了多少时间"而不是"现在几点"。
      单调时钟不会因NTP调整而跳变。
      CyberRT也是这样做的(CLOCK_MONOTONIC_RAW)。
    """

    _instance = None
    _lock = threading.Lock()

    @classmethod
    def instance(cls) -> 'Clock':
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = cls()
        return cls._instance

    def __init__(self):
        self._mode = ClockMode.CYBER
        self._mock_now_ns: int = 0
        self._lock = threading.Lock()

    @classmethod
    def now(cls) -> int:
        """返回当前时间(纳秒) — CyberRT: Clock::Now()"""
        inst = cls.instance()
        if inst._mode == ClockMode.CYBER:
            return time.monotonic_ns()
        return inst._mock_now_ns

    @classmethod
    def now_sec(cls) -> float:
        """返回当前时间(秒) — CyberRT: Clock::NowInSeconds()"""
        return cls.now() / 1_000_000_000

    @classmethod
    def set_now(cls, now_ns: int):
        """MOCK模式: 设置当前时间 — CyberRT: Clock::SetNow()"""
        inst = cls.instance()
        with inst._lock:
            inst._mock_now_ns = now_ns

    @classmethod
    def set_mode(cls, mode: ClockMode):
        """切换时钟模式 — CyberRT: Clock::SetMode()"""
        inst = cls.instance()
        with inst._lock:
            inst._mode = mode
            if mode == ClockMode.MOCK:
                inst._mock_now_ns = time.monotonic_ns()

    @classmethod
    def mode(cls) -> ClockMode:
        return cls.instance()._mode


class Rate:
    """
    频率控制器 — 固定频率循环

    CyberRT来源 (rate.h):
      Rate(frequency_hz)
      Sleep() → 睡眠到下一次触发时间

    使用:
      rate = Rate(30.0)  # 30Hz
      while running:
          do_work()
          rate.sleep()  # 精确等待到下一个33.3ms边界

    WorldTickDriver 内部使用 Rate(30) 控制tick频率。
    """

    def __init__(self, frequency_hz: float):
        self._period_ns = int(1_000_000_000 / frequency_hz)
        self._last_ns = Clock.now()
        self._actual_period_ns = self._period_ns

    def sleep(self):
        """
        睡到下一个周期边界

        与naive的time.sleep(1/30)不同:
        Rate.sleep()补偿了work时间, 保证周期稳定。
        如果work花了10ms, 只需再睡23.3ms。
        如果work超过了一个周期, 立即返回(不累积延迟)。
        """
        now = Clock.now()
        expected = self._last_ns + self._period_ns
        sleep_ns = expected - now

        if sleep_ns > 0 and Clock.mode() == ClockMode.CYBER:
            time.sleep(sleep_ns / 1_000_000_000)

        actual_now = Clock.now()
        self._actual_period_ns = actual_now - self._last_ns
        self._last_ns = actual_now

    @property
    def period_ns(self) -> int:
        return self._period_ns

    @property
    def actual_period_ns(self) -> int:
        """上一个周期的实际长度(用于监控jitter)"""
        return self._actual_period_ns


# ── TimingWheel + Timer ──────────────────────────────────
#
# CyberRT来源:
#   cyber/timer/timing_wheel.h → 时间轮, 管理大量定时器
#   cyber/timer/timer_bucket.h → 时间轮的一个格子
#   cyber/timer/timer.h → 用户接口
#
# 时间轮原理:
#   把时间轴分成固定大小的格子(slot), 每个格子里放着
#   在该时刻触发的定时器。一个指针按固定频率旋转(tick),
#   到达格子时触发其中的所有定时器。
#
#   优势: AddTimer O(1), Tick O(k) k=当前slot的定时器数
#   劣势: 精度受限于slot分辨率(默认1ms)
#
# pubsub-loop使用:
#   - WorldTickDriver的30Hz心跳 → 直接用Rate, 不用TimingWheel
#   - 治理超时(governance timeout) → Timer
#   - 统计定期输出 → Timer
#   - 个体行为的延迟触发 → Timer

@dataclass
class TimerTask:
    """定时器任务"""
    task_id: int
    callback: Callable[[], None]
    fire_tick: int         # 触发时的tick计数
    period_ticks: int      # 周期(单位: wheel tick)
    oneshot: bool          # 是否单次触发
    cancelled: bool = False


class TimingWheel:
    """
    时间轮 — 高效定时器管理

    CyberRT来源 (timing_wheel.h):
      resolution: 1ms per tick
      slots: 512 buckets
      Tick(): 推进一格, 触发到期定时器
      AddTimer(task): 注册定时器

    1000个个体场景:
      假设每个个体有2个定时器(超时+心跳) → 2000个定时器
      AddTimer: O(1), 插入到对应slot
      Tick: O(k), k = 当前slot的定时器数 ≈ 2000/512 ≈ 4 — 可忽略
    """

    def __init__(self, num_slots: int = 512,
                 resolution_ms: float = 1.0):
        self._num_slots = num_slots
        self._resolution_ns = int(resolution_ms * 1_000_000)
        self._slots: List[List[TimerTask]] = [[] for _ in range(num_slots)]
        self._current_slot = 0
        self._tick_count = 0
        self._next_id = 0
        self._lock = threading.Lock()
        self._running = False
        self._thread: Optional[threading.Thread] = None

    def add_timer(self, delay_ms: float,
                  callback: Callable[[], None],
                  period_ms: float = 0,
                  oneshot: bool = True) -> int:
        """
        注册定时器

        CyberRT: TimingWheel::AddTimer(task)
        delay_ms: 首次触发延迟
        period_ms: 周期(0=单次)
        """
        delay_ticks = max(1, int(delay_ms / (self._resolution_ns / 1_000_000)))
        period_ticks = int(period_ms / (self._resolution_ns / 1_000_000)) if period_ms > 0 else 0

        with self._lock:
            task_id = self._next_id
            self._next_id += 1

            fire_tick = self._tick_count + delay_ticks
            slot_idx = (self._current_slot + delay_ticks) % self._num_slots

            task = TimerTask(
                task_id=task_id,
                callback=callback,
                fire_tick=fire_tick,
                period_ticks=period_ticks,
                oneshot=oneshot,
            )
            self._slots[slot_idx].append(task)
        return task_id

    def cancel_timer(self, task_id: int):
        """取消定时器 — 惰性删除(标记cancelled, 在tick时清理)"""
        with self._lock:
            for slot in self._slots:
                for task in slot:
                    if task.task_id == task_id:
                        task.cancelled = True
                        return

    def tick(self):
        """
        推进一格 — 触发到期定时器

        CyberRT: TimingWheel::Tick()
        """
        with self._lock:
            self._tick_count += 1
            self._current_slot = (self._current_slot + 1) % self._num_slots
            slot = self._slots[self._current_slot]

            # 分离: 到期的 vs 未到期的
            fired = []
            remaining = []
            for task in slot:
                if task.cancelled:
                    continue
                if task.fire_tick <= self._tick_count:
                    fired.append(task)
                else:
                    remaining.append(task)
            self._slots[self._current_slot] = remaining

        # 在锁外执行回调(避免死锁)
        reschedule = []
        for task in fired:
            try:
                task.callback()
            except Exception:
                pass
            # 周期定时器: 重新调度
            if not task.oneshot and task.period_ticks > 0:
                task.fire_tick = self._tick_count + task.period_ticks
                reschedule.append(task)

        if reschedule:
            with self._lock:
                for task in reschedule:
                    slot_idx = (self._current_slot + task.period_ticks) % self._num_slots
                    self._slots[slot_idx].append(task)

    @property
    def tick_count(self) -> int:
        return self._tick_count

    def start(self):
        """启动时间轮后台线程"""
        self._running = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=1.0)

    def _run(self):
        while self._running:
            self.tick()
            time.sleep(self._resolution_ns / 1_000_000_000)


class Timer:
    """
    定时器 — 用户接口

    CyberRT来源 (timer.h):
      Timer(period_ms, callback, oneshot)
      Start() → 启动
      Stop() → 停止

    使用:
      # 周期定时器: 每1000ms执行一次
      timer = Timer(period_ms=1000, callback=print_stats, oneshot=False)
      timer.start()

      # 单次定时器: 5000ms后超时
      timeout = Timer(period_ms=5000, callback=on_timeout, oneshot=True)
      timeout.start()
    """

    # 全局共享的TimingWheel实例
    _wheel: Optional[TimingWheel] = None
    _wheel_lock = threading.Lock()

    @classmethod
    def _get_wheel(cls) -> TimingWheel:
        if cls._wheel is None:
            with cls._wheel_lock:
                if cls._wheel is None:
                    cls._wheel = TimingWheel()
                    cls._wheel.start()
        return cls._wheel

    def __init__(self, period_ms: float,
                 callback: Callable[[], None],
                 oneshot: bool = False):
        self._period_ms = period_ms
        self._callback = callback
        self._oneshot = oneshot
        self._task_id: Optional[int] = None

    def start(self):
        """启动定时器"""
        wheel = self._get_wheel()
        self._task_id = wheel.add_timer(
            delay_ms=self._period_ms,
            callback=self._callback,
            period_ms=0 if self._oneshot else self._period_ms,
            oneshot=self._oneshot,
        )

    def stop(self):
        """停止定时器"""
        if self._task_id is not None:
            self._get_wheel().cancel_timer(self._task_id)
            self._task_id = None
