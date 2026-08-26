# SPDX-License-Identifier: Apache-2.0
#
# world/proto/world_types.py
#
# 世界主循环核心协议类型
#
# 来源映射:
#   CyberRT  →  WorldTick / MotionRequest / ConfirmedState (proto messages)
#   governance-toolkit  →  InterventionPoint / ApprovalRequest / ExecutionDecision
#   PRD #1449 / #1450 / #1451 / #1452
#
# 语义:
#   个体不能单方面决定自己的最终位置。
#   个体只能"申请"运动(MotionRequest)，世界解算器(WorldResolver)
#   收集所有申请 → 碰撞检测 → 约束求解 → 发布 ConfirmedState。
#   这就是"只要动了就需要申请说自己要动了"的客观约束。

from __future__ import annotations
from dataclasses import dataclass, field
from enum import IntFlag, IntEnum
from typing import Optional, List


# ─── 基础几何类型 ───

@dataclass(slots=True)
class Vec3:
    """三维向量 — 位置/速度/力"""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

    def __add__(self, other: Vec3) -> Vec3:
        return Vec3(self.x + other.x, self.y + other.y, self.z + other.z)

    def __sub__(self, other: Vec3) -> Vec3:
        return Vec3(self.x - other.x, self.y - other.y, self.z - other.z)

    def __mul__(self, s: float) -> Vec3:
        return Vec3(self.x * s, self.y * s, self.z * s)

    def dot(self, other: Vec3) -> float:
        return self.x * other.x + self.y * other.y + self.z * other.z

    def length_sq(self) -> float:
        return self.dot(self)


@dataclass(slots=True)
class Quat:
    """四元数 — 旋转表示"""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    w: float = 1.0  # 单位四元数默认


# ─── 世界时钟 (来自 CyberRT TimerComponent → PRD #1450) ───

@dataclass(slots=True)
class WorldTick:
    """
    世界心跳 — WorldTickDriver @ 30Hz 发布

    CyberRT对应: TimerComponent定时触发
    governance对应: 无直接对应(governance是事件驱动，无全局时钟)
    """
    tick_seq: int           # 单调递增的tick序号
    delta_time_s: float     # 本tick的dt (通常 1/30 ≈ 0.0333s)
    wall_time_ns: int       # 系统墙钟时间(纳秒)


# ─── 运动申请 (来自 CyberRT proto → PRD #1451) ───

class CollisionLayer(IntFlag):
    """碰撞层掩码 — 决定哪些个体之间检测碰撞

    governance对应: ExecutionRing (RING_0~3信任分级)
      RING_0_KERNEL  → STATIC (不碰撞，直接通过)
      RING_1_TRUSTED → DEFAULT (正常碰撞检测)
      RING_2_LIMITED → DEFAULT
      RING_3_SANDBOX → SANDBOX (只与同层碰撞)
    """
    NONE    = 0
    DEFAULT = 1 << 0   # 标准碰撞层
    STATIC  = 1 << 1   # 静态障碍物(墙/地板)
    TRIGGER = 1 << 2   # 只检测进入/离开，不产生物理响应
    SANDBOX = 1 << 3   # 沙箱层(governance RING_3)


@dataclass(slots=True)
class MotionRequest:
    """
    个体运动意图申请 — 个体只能申请，不能单方面决定

    CyberRT语义: 个体收到WorldTick → 计算本tick想做的运动 → pub motion_request
    governance语义: agent发起ApprovalRequest → 等待审批链确认

    关键字段映射:
      individual_id  ←  governance.agent_id
      tick_seq       ←  governance.policy_decision_id (绑定到具体请求)
      delta_position ←  governance.operation (具体的动作内容)
      priority       ←  governance.ExecutionRing.eff_score (信任分数)
      collision_mask ←  governance.ExecutionRing.ring_level (信任等级)
    """
    individual_id: int
    tick_seq: int                                    # 响应的WorldTick.tick_seq

    delta_position: Vec3 = field(default_factory=Vec3)   # 想要的位置变化量
    delta_rotation: Quat = field(default_factory=Quat)   # 想要的旋转变化量(增量)
    applied_force:  Optional[Vec3] = None            # 施加的力(连续动力学)
    applied_torque: Optional[Vec3] = None            # 施加的扭矩

    priority: float = 0.0                            # 运动优先级(高=governance高信任ring)
    collision_mask: int = CollisionLayer.DEFAULT      # 碰撞层掩码


# ─── 世界确认状态 (来自 CyberRT proto → PRD #1451/#1452) ───

class ResolveFlag(IntFlag):
    """世界解算标志 — 告诉个体"世界对你的申请做了什么"

    governance对应: ExecutionDecision.reason_code
    """
    NONE           = 0
    WAS_COLLIDED   = 1 << 0   # 发生了碰撞
    WAS_CORRECTED  = 1 << 1   # 位置被约束修正
    REQUEST_DENIED = 1 << 2   # 运动申请被完全拒绝(governance deny)


@dataclass(slots=True)
class ConfirmedState:
    """
    世界确认的个体最终状态 — 只有WorldResolver有权发布

    CyberRT语义: WorldResolver完成碰撞检测+约束求解后发布
    governance语义: ApprovalCoordinator完成审批链后返回ExecutionDecision

    关键约束:
      1. 个体不能自己发布ConfirmedState
      2. ConfirmedState一旦发布就是世界事实(不可逆 — 与governance的saga补偿不同)
      3. 下一tick的MotionRequest从ConfirmedState出发
    """
    individual_id: int
    tick_seq: int

    position: Vec3 = field(default_factory=Vec3)     # 世界确认的最终位置
    rotation: Quat = field(default_factory=Quat)     # 世界确认的最终旋转
    velocity: Optional[Vec3] = None                  # 确认后的速度(展示层插值用)
    angular_velocity: Optional[Vec3] = None

    resolve_flags: int = ResolveFlag.NONE


# ─── 碰撞事件 (可选订阅，用于debug/stats) ───

@dataclass(slots=True)
class CollisionEvent:
    """
    碰撞事件 — 可选订阅

    CyberRT对应: CollisionEventBatch channel
    governance对应: HypervisorEvent (event_type=COLLISION)
    """
    individual_a: int
    individual_b: int
    contact_point: Vec3 = field(default_factory=Vec3)
    contact_normal: Vec3 = field(default_factory=Vec3)
    penetration_depth: float = 0.0
    tick_seq: int = 0


# ─── 治理拦截结果 (来自 governance InterventionPoint → PRD #1649) ───

class GovernanceDecision(IntEnum):
    """治理拦截器的裁定

    直接来自 governance-toolkit 的 verdict.Decision 枚举
    映射到CyberRT: 作为WorldResolver的pre-solve hook
    """
    ALLOW            = 0   # 允许，进入物理解算
    DENY             = 1   # 拒绝，MotionRequest被丢弃
    REQUIRE_APPROVAL = 2   # 需要额外审批(governance saga)
    MODIFY           = 3   # 允许但修改参数(如降低priority)


@dataclass(slots=True)
class GovernanceVerdict:
    """
    治理拦截器对单个MotionRequest的裁定结果

    governance-toolkit来源:
      runtime.evaluate_intervention_point() → InterventionPointResult.verdict
    映射:
      decision → allow/deny/modify
      reason   → verdict.reason
      modified_request → 如果decision=MODIFY，包含修改后的参数
    """
    decision: GovernanceDecision
    reason: Optional[str] = None
    modified_priority: Optional[float] = None
    modified_collision_mask: Optional[int] = None
