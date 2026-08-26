# SPDX-License-Identifier: Apache-2.0
#
# world/component/tick_driver.py
#
# WorldTickDriver — 世界 30Hz 心跳驱动器
#
# CyberRT来源:
#   cyber::TimerComponent — 定时触发，不绑定channel输入
#   cyber/timer/timer.h — Timer类，回调驱动
#   PRD #1450
#
# governance来源:
#   无直接对应 — governance是事件驱动，没有全局时钟。
#   但 WorldTickDriver 扮演的角色类似 SagaOrchestrator 的节拍器：
#   governance是"请求到了才处理"，cyberRT是"时钟到了统一处理"。
#
# 语义:
#   WorldTickDriver 是整个世界主循环的起搏器。
#   每个tick发布 WorldTick 消息 → 所有个体收到后提交 MotionRequest
#   → WorldResolver 收集并解算 → 发布 ConfirmedState。
#
#   一个tick内不允许二次申请（与governance saga"步骤顺序执行"对应）

import time
import threading
from typing import Optional
from world.proto.world_types import WorldTick
from world.proto.channels import (
    ChannelManager, Writer, CHANNEL_WORLD_TICK
)


class WorldTickDriver:
    """
    世界心跳驱动器 — 30Hz 定时发布 WorldTick

    CyberRT对应:
      class WorldTickDriver : public TimerComponent {
          bool Init() override { /* 创建 writer */ }
          bool Proc() override { /* 发布 tick */ }
      };

    使用:
      driver = WorldTickDriver(channel_manager, tick_rate_hz=30)
      driver.start()  # 开始发布心跳
      driver.stop()   # 停止
    """

    def __init__(self, channel_manager: ChannelManager,
                 tick_rate_hz: float = 30.0):
        self._channel_manager = channel_manager
        self._tick_rate_hz = tick_rate_hz
        self._tick_interval_s = 1.0 / tick_rate_hz
        self._tick_seq: int = 0
        self._running = False
        self._thread: Optional[threading.Thread] = None

        # Init — 对应 CyberRT Component::Init()
        self._tick_writer: Writer[WorldTick] = channel_manager.create_writer(
            CHANNEL_WORLD_TICK, WorldTick
        )

    def start(self):
        """启动心跳 — 对应 CyberRT mainboard 加载 Component"""
        if self._running:
            return
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self):
        """停止心跳"""
        self._running = False
        if self._thread:
            self._thread.join(timeout=1.0)

    def _loop(self):
        """主循环 — 对应 CyberRT Timer 的回调触发"""
        while self._running:
            t_start = time.monotonic_ns()
            self._tick_seq += 1

            tick = WorldTick(
                tick_seq=self._tick_seq,
                delta_time_s=self._tick_interval_s,
                wall_time_ns=time.time_ns()
            )

            # Proc — 对应 CyberRT TimerComponent::Proc()
            self._tick_writer.write(tick)

            # 保持固定频率
            elapsed_s = (time.monotonic_ns() - t_start) / 1e9
            sleep_s = max(0, self._tick_interval_s - elapsed_s)
            if sleep_s > 0:
                time.sleep(sleep_s)

    def tick_once(self) -> WorldTick:
        """
        手动触发一个tick — 用于同步测试 / 单步调试

        不在定时循环里，直接调用一次Proc逻辑。
        """
        self._tick_seq += 1
        tick = WorldTick(
            tick_seq=self._tick_seq,
            delta_time_s=self._tick_interval_s,
            wall_time_ns=time.time_ns()
        )
        self._tick_writer.write(tick)
        return tick

    @property
    def current_tick_seq(self) -> int:
        return self._tick_seq
