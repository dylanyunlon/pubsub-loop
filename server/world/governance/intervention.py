# SPDX-License-Identifier: Apache-2.0
#
# world/governance/intervention.py
#
# 治理拦截层 — 从 agent-governance-toolkit 移植
#
# governance来源:
#   policy-engine/core/src/runtime.rs:
#     Runtime.evaluate_intervention_point()
#     Runtime.evaluate_intervention_point_inner()
#
#   关键调用链 (runtime.rs:209-306):
#     1. point_config = manifest.intervention_points[point]
#     2. policy_target = resolve(policy_target_path)
#     3. tool = project_tool(manifest, point, config, snapshot)
#     4. policy_input = build_policy_input(point, target, snapshot, annotations, tool)
#     5. annotations = annotator.annotate(policy_input)
#     6. verdict = policy.evaluate(prepared_invocation)
#     7. decision = normalize_policy_output(verdict)
#
# CyberRT映射 (PRD #1649):
#   InterventionPoint → WorldResolver 的 pre-solve hook
#   motion_request → governance_filter() → broadphase
#   请求 → 打标签 → 评估策略 → 裁定 allow/deny/modify
#
# 当前实现:
#   简化版 — 基于规则的拦截器，不需要 Cedar 策略语言。
#   后续: 如果需要声明式规则，可以集成 governance 的策略引擎。

from dataclasses import dataclass, field
from typing import List, Callable, Optional, Dict
from enum import Enum
from world.proto.world_types import (
    MotionRequest, GovernanceDecision, GovernanceVerdict,
    CollisionLayer, Vec3,
)


# ─── 信任环 (来自 governance ExecutionRing) ───

class TrustRing(Enum):
    """
    信任环等级 — 直接来自 governance-toolkit ExecutionRing

    governance来源:
      RING_0_KERNEL  → 最高信任，不受约束
      RING_1_TRUSTED → 正常信任，标准碰撞检测
      RING_2_LIMITED → 受限，降低优先级
      RING_3_SANDBOX → 沙箱隔离，只与同层碰撞

    CyberRT映射 (PRD #1649):
      RING_0 → priority=1.0, collision_mask=NONE (不碰撞)
      RING_1 → priority=0.8, collision_mask=DEFAULT
      RING_2 → priority=0.3, collision_mask=DEFAULT
      RING_3 → priority=0.1, collision_mask=SANDBOX
    """
    RING_0_KERNEL  = 0
    RING_1_TRUSTED = 1
    RING_2_LIMITED = 2
    RING_3_SANDBOX = 3


# 信任环 → CyberRT参数映射
RING_TO_PRIORITY = {
    TrustRing.RING_0_KERNEL:  1.0,
    TrustRing.RING_1_TRUSTED: 0.8,
    TrustRing.RING_2_LIMITED: 0.3,
    TrustRing.RING_3_SANDBOX: 0.1,
}

RING_TO_COLLISION_MASK = {
    TrustRing.RING_0_KERNEL:  CollisionLayer.NONE,
    TrustRing.RING_1_TRUSTED: CollisionLayer.DEFAULT | CollisionLayer.STATIC,
    TrustRing.RING_2_LIMITED: CollisionLayer.DEFAULT | CollisionLayer.STATIC,
    TrustRing.RING_3_SANDBOX: CollisionLayer.SANDBOX,
}


# ─── 拦截规则 ───

@dataclass
class InterventionRule:
    """
    单条拦截规则

    governance来源:
      manifest.yaml → intervention_points → policy 配置
      每个 intervention_point 有:
        - policy_target: 要评估的目标字段
        - policy: 策略ID
        - annotators: 标注器列表
    """
    name: str
    description: str = ""
    predicate: Optional[Callable[[MotionRequest], bool]] = None
    action: GovernanceDecision = GovernanceDecision.ALLOW


class GovernanceInterceptor:
    """
    治理拦截器 — WorldResolver 的 pre-solve hook

    governance来源:
      Runtime {
          manifest: Manifest,        → rules (规则配置)
          annotations: Annotator,    → _annotate (打标签)
          policy: PolicyDispatcher,  → _evaluate (策略评估)
          telemetry: TelemetrySink,  → _audit_log (审计日志)
      }

    使用:
      interceptor = GovernanceInterceptor()
      interceptor.register_individual(id=1, ring=TrustRing.RING_1_TRUSTED)
      interceptor.add_rule(rule)

      # 注入 WorldResolver:
      resolver = WorldResolver(channel_manager,
                               governance_filter=interceptor.evaluate)
    """

    def __init__(self):
        self._individual_rings: Dict[int, TrustRing] = {}
        self._rules: List[InterventionRule] = []
        self._audit_log: List[Dict] = []  # governance EventBus 简化版

    def register_individual(self, individual_id: int,
                            ring: TrustRing = TrustRing.RING_1_TRUSTED):
        """注册个体的信任等级"""
        self._individual_rings[individual_id] = ring

    def add_rule(self, rule: InterventionRule):
        """添加拦截规则 — 对应 governance manifest 中的 policy 配置"""
        self._rules.append(rule)

    def evaluate(self, request: MotionRequest) -> GovernanceVerdict:
        """
        评估单个 MotionRequest — 对应 governance Runtime.evaluate_intervention_point()

        流程:
          1. 查找个体信任环 (governance: 查 agent trust score)
          2. 应用信任环参数 (governance: ring → eff_score → 权限)
          3. 评估拦截规则 (governance: annotate → evaluate policy)
          4. 返回裁定 (governance: verdict)
        """
        iid = request.individual_id
        ring = self._individual_rings.get(iid, TrustRing.RING_1_TRUSTED)

        # Step 1: 信任环映射
        mapped_priority = RING_TO_PRIORITY[ring]
        mapped_mask = RING_TO_COLLISION_MASK[ring]

        # Step 2: 评估规则
        for rule in self._rules:
            if rule.predicate and rule.predicate(request):
                verdict = GovernanceVerdict(
                    decision=rule.action,
                    reason=f"rule:{rule.name}",
                )
                self._record_audit(request, verdict, rule.name)
                return verdict

        # Step 3: 默认裁定 — 允许但应用信任环参数
        verdict = GovernanceVerdict(
            decision=GovernanceDecision.MODIFY,
            reason=f"ring:{ring.name}",
            modified_priority=mapped_priority,
            modified_collision_mask=mapped_mask,
        )
        self._record_audit(request, verdict, "ring_default")
        return verdict

    def _record_audit(self, request: MotionRequest,
                      verdict: GovernanceVerdict, source: str):
        """
        审计记录 — 对应 governance HypervisorEventBus.emit()

        governance来源 (PRD #1649):
          EventBus.emit(event) → 内存 deque + notify subscribers
        CyberRT映射:
          EventLog.record(event) → channel pub + 持久化(可选)
        """
        self._audit_log.append({
            "individual_id": request.individual_id,
            "tick_seq": request.tick_seq,
            "decision": verdict.decision.name,
            "reason": verdict.reason,
            "source": source,
        })

    @property
    def audit_log(self) -> List[Dict]:
        return self._audit_log
