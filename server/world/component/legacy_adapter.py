# SPDX-License-Identifier: Apache-2.0
#
# world/component/legacy_adapter.py
#
# LegacyStateAdapter — 兼容层
#
# PRD #1453: confirmed_state 语义迁移
#   将个体直接 publish position 替换为 motion_request → 世界确认。
#   旧代码不需要改，LegacyStateAdapter 透明拦截。
#
# CyberRT来源:
#   旧模式: writer->Write(my_position) → 直接被消费
#   新模式: writer->Write(motion_request) → WorldResolver解算 → confirmed_state
#
# governance来源 (PRD #1649):
#   InterventionPoint — 在请求生命周期的关键点拦截
#   LegacyStateAdapter 就是一个 intervention point:
#     拦截旧的 "直接publish position" 调用
#     转换为 MotionRequest
#     非物理属性(颜色、标签等)直接通过

from typing import Optional, Dict
from world.proto.world_types import (
    Vec3, Quat, MotionRequest, ConfirmedState,
    CollisionLayer,
)
from world.proto.channels import (
    ChannelManager, Writer, CHANNEL_MOTION_REQUEST,
)


class LegacyStateAdapter:
    """
    兼容适配器 — 旧代码透明迁移到 motion_request 模式

    旧API (消息队列语义):
      individual.set_position(x, y, z)  → 直接生效

    新API (物理引擎语义, 通过适配器透明转换):
      individual.set_position(x, y, z)
        → adapter.intercept(id, new_pos)
        → 计算 delta = new_pos - confirmed_pos
        → pub MotionRequest(delta)
        → WorldResolver 解算后发布 ConfirmedState
        → 实际位置可能与请求不同(碰撞修正)

    governance对应 (PRD #1649):
      adapter ≈ InterventionPoint
      拦截旧API → 转换为新协议
      非物理属性(颜色、名称等)直接通过
    """

    def __init__(self, channel_manager: ChannelManager,
                 get_tick_seq: callable):
        self._channel_manager = channel_manager
        self._get_tick_seq = get_tick_seq

        # 发布 MotionRequest
        self._request_writer: Writer[MotionRequest] = channel_manager.create_writer(
            CHANNEL_MOTION_REQUEST, MotionRequest
        )

        # 记录每个个体的"旧API声称位置" — 用于计算 delta
        self._legacy_positions: Dict[int, Vec3] = {}

        # 个体的当前确认位置引用(从WorldResolver获取)
        self._confirmed_positions: Dict[int, Vec3] = {}

    def update_confirmed(self, individual_id: int, position: Vec3):
        """WorldResolver 发布 ConfirmedState 时更新"""
        self._confirmed_positions[individual_id] = position

    def intercept_set_position(self, individual_id: int,
                                new_position: Vec3,
                                priority: float = 0.0) -> MotionRequest:
        """
        拦截旧的 set_position 调用 → 转换为 MotionRequest

        PRD #1453 核心语义:
          旧: individual.position = (10, 0, 0) → 直接生效
          新: individual.position = (10, 0, 0)
              → delta = (10,0,0) - confirmed_position
              → pub MotionRequest(delta)
              → 等待 WorldResolver 确认
        """
        # 当前确认位置
        confirmed = self._confirmed_positions.get(
            individual_id, Vec3(0, 0, 0)
        )

        # 计算 delta
        delta = new_position - confirmed

        # 构建 MotionRequest
        request = MotionRequest(
            individual_id=individual_id,
            tick_seq=self._get_tick_seq(),
            delta_position=delta,
            priority=priority,
            collision_mask=CollisionLayer.DEFAULT,
        )

        # 发布
        self._request_writer.write(request)

        # 记录旧API声称位置(debug用)
        self._legacy_positions[individual_id] = new_position

        return request

    def get_legacy_position(self, individual_id: int) -> Optional[Vec3]:
        """获取旧API声称的位置(不是世界确认的)"""
        return self._legacy_positions.get(individual_id)

    def get_position_drift(self, individual_id: int) -> Optional[Vec3]:
        """
        获取"旧API声称位置"与"世界确认位置"的偏差

        如果 drift 很大，说明旧代码的位置设定被碰撞系统大幅修正了。
        这是帮助旧代码调试的重要指标。
        """
        legacy = self._legacy_positions.get(individual_id)
        confirmed = self._confirmed_positions.get(individual_id)
        if legacy is None or confirmed is None:
            return None
        return legacy - confirmed
