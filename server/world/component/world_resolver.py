# SPDX-License-Identifier: Apache-2.0
#
# world/component/world_resolver.py
#
# WorldResolver — 世界解算器
#
# CyberRT来源:
#   class WorldResolver : public Component<WorldTick>
#     Init()  → CreateReader(motion_request), CreateWriter(confirmed_state)
#     Proc()  → collect → broadphase → narrowphase → solve → commit
#   PRD #1452
#
# governance来源:
#   Runtime.evaluate_intervention_point() → annotate → evaluate → decision
#   SagaOrchestrator — create_saga → add_step → execute_step
#   ApprovalCoordinator — open_request → submit_entry → resolve
#   PRD #1649 移植策略
#
# 关键约束:
#   1. WorldResolver 是唯一的 ConfirmedState 发布者(单点写入权威)
#   2. 个体不能自己发布 ConfirmedState
#   3. ConfirmedState 一旦发布就是世界事实(不可逆)

import threading
from typing import Dict, List, Tuple, Optional, Callable
from world.proto.world_types import (
    WorldTick, MotionRequest, ConfirmedState, CollisionEvent,
    Vec3, Quat, ResolveFlag, CollisionLayer,
    GovernanceDecision, GovernanceVerdict,
)
from world.proto.channels import (
    ChannelManager, Writer, Reader,
    CHANNEL_WORLD_TICK, CHANNEL_MOTION_REQUEST,
    CHANNEL_CONFIRMED_STATE, CHANNEL_COLLISION_EVENTS,
)


class WorldResolver:
    """
    世界解算器 — 收集 MotionRequest → 碰撞检测 → 约束求解 → 发布 ConfirmedState

    CyberRT对应:
      class WorldResolver : public Component<WorldTick> {
          bool Init() override;
          bool Proc(const shared_ptr<WorldTick>& tick) override;
      };

    governance对应:
      Runtime {
          manifest, annotations, policy, telemetry
          evaluate_intervention_point(request) → verdict
      }

    Phase 流程 (PRD #1449):
      Phase 1: Collect    — 个体提交 MotionRequest (≤5ms)
      Phase 2a: Broad     — spatial hash 碰撞粗筛 (≤3ms)
      Phase 2b: Narrow    — per-pair 精确碰撞检测 (≤4ms)
      Phase 2c: Solve     — 约束求解 + 位置校正 (≤3ms)
      Phase 2d: Commit    — pub confirmed_state (≤2ms)
    """

    def __init__(self, channel_manager: ChannelManager,
                 cell_size: float = 2.0,
                 solver_iterations: int = 4,
                 governance_filter: Optional[Callable[[MotionRequest], GovernanceVerdict]] = None):
        """
        Init — 对应 CyberRT Component::Init()

        governance_filter: 可选的治理拦截器，在物理解算前拦截 MotionRequest。
          来自 governance-toolkit 的 InterventionPoint 模式 (PRD #1649):
            motion_request → governance_filter() → broadphase → narrowphase → solve
        """
        self._channel_manager = channel_manager
        self._cell_size = cell_size
        self._solver_iterations = solver_iterations
        self._governance_filter = governance_filter

        # 个体的当前确认状态 — 世界权威状态存储
        self._confirmed_states: Dict[int, ConfirmedState] = {}

        # 本tick收集的 pending requests
        self._pending_requests: List[MotionRequest] = []
        self._request_lock = threading.Lock()

        # ── CyberRT Init(): 创建 readers 和 writers ──

        # 订阅 motion_request
        self._request_reader = channel_manager.create_reader(
            CHANNEL_MOTION_REQUEST, MotionRequest,
            callback=self._on_motion_request
        )

        # 创建 confirmed_state 发布者
        self._confirmed_writer: Writer[ConfirmedState] = channel_manager.create_writer(
            CHANNEL_CONFIRMED_STATE, ConfirmedState
        )

        # 创建碰撞事件发布者(可选订阅)
        self._collision_writer: Writer[CollisionEvent] = channel_manager.create_writer(
            CHANNEL_COLLISION_EVENTS, CollisionEvent
        )

        # 订阅 world tick — 触发 Proc
        self._tick_reader = channel_manager.create_reader(
            CHANNEL_WORLD_TICK, WorldTick,
            callback=self._on_tick
        )

    # ── Callbacks ──

    def _on_motion_request(self, request: MotionRequest):
        """收到个体的运动申请 — 加入 pending 队列"""
        with self._request_lock:
            self._pending_requests.append(request)

    def _on_tick(self, tick: WorldTick):
        """
        收到 WorldTick 时触发 — 对应 CyberRT Component::Proc()

        这是世界主循环的核心入口。
        """
        self.proc(tick)

    # ── Phase 流程 ──

    def proc(self, tick: WorldTick) -> List[ConfirmedState]:
        """
        世界解算主流程 — 对应 CyberRT WorldResolver::Proc()

        governance对应:
          Runtime.evaluate_intervention_point_inner():
            1. 获取 intervention_point 配置
            2. 构建 policy_input
            3. annotate (打标签)
            4. evaluate policy (策略评估)
            5. 返回 verdict
        """
        tick_seq = tick.tick_seq

        # ── Phase 2a: Collect ──
        with self._request_lock:
            requests = list(self._pending_requests)
            self._pending_requests.clear()

        # 过滤: 只处理 tick_seq 匹配的(或 tick_seq-1 的迟到请求)
        requests = [r for r in requests
                     if r.tick_seq == tick_seq or r.tick_seq == tick_seq - 1]

        # 没有 request → 所有个体保持原位
        if not requests:
            return self._publish_unchanged(tick_seq)

        # ── 治理拦截 (governance pre-solve hook, PRD #1649) ──
        if self._governance_filter:
            requests = self._apply_governance(requests)

        # ── Phase 2a: Broadphase — spatial hash 碰撞候选对 ──
        candidates = self._broadphase(requests)

        # ── Phase 2b: Narrowphase — per-pair 精确碰撞检测 ──
        collisions = self._narrowphase(candidates, requests)

        # ── Phase 2c: Solve — 约束求解 + 位置校正 ──
        resolved = self._constraint_solve(requests, collisions, tick.delta_time_s)

        # ── Phase 2d: Commit — 发布 ConfirmedState ──
        confirmed = self._commit(resolved, tick_seq)

        # 发布碰撞事件(可选)
        for col in collisions:
            self._collision_writer.write(col)

        return confirmed

    # ── 治理拦截 ──

    def _apply_governance(self, requests: List[MotionRequest]) -> List[MotionRequest]:
        """
        治理拦截器 — governance pre-solve hook

        governance来源 (runtime.rs):
          evaluate_intervention_point():
            point_config = manifest.intervention_points[point]
            policy_target = resolve(policy_target_path)
            policy_input = build_policy_input(...)
            annotations = annotate(policy_input)
            verdict = policy.evaluate(invocation)
            return verdict

        CyberRT映射 (PRD #1649):
          motion_request → governance_filter() → broadphase
          ALLOW → 进入物理解算
          DENY  → MotionRequest被丢弃
          MODIFY → 修改参数后进入物理解算
        """
        filtered = []
        for req in requests:
            verdict = self._governance_filter(req)
            if verdict.decision == GovernanceDecision.ALLOW:
                filtered.append(req)
            elif verdict.decision == GovernanceDecision.MODIFY:
                if verdict.modified_priority is not None:
                    req.priority = verdict.modified_priority
                if verdict.modified_collision_mask is not None:
                    req.collision_mask = verdict.modified_collision_mask
                filtered.append(req)
            elif verdict.decision == GovernanceDecision.DENY:
                # 发布被拒绝的 ConfirmedState (保持原位 + REQUEST_DENIED flag)
                if req.individual_id in self._confirmed_states:
                    denied = self._confirmed_states[req.individual_id]
                    denied.resolve_flags = ResolveFlag.REQUEST_DENIED
                    denied.tick_seq = req.tick_seq
                    self._confirmed_writer.write(denied)
            # REQUIRE_APPROVAL: 当前实现中等同 DENY，后续PRD扩展
        return filtered

    # ── Broadphase ──

    def _broadphase(self, requests: List[MotionRequest]
                    ) -> List[Tuple[MotionRequest, MotionRequest]]:
        """
        Broadphase — spatial hash 碰撞候选对筛选

        CyberRT来源: world::data::SpatialHash
        PRD #1452: sweep-and-prune / spatial hash, ≤3ms budget

        当前实现: 简化版 — O(n²) pairwise check。
        后续优化: 移植 CyberRT 的 SpatialHash (PRD #1455)
        """
        candidates = []
        for i in range(len(requests)):
            for j in range(i + 1, len(requests)):
                a, b = requests[i], requests[j]
                # 检查碰撞层掩码
                if not (a.collision_mask & b.collision_mask):
                    continue
                # 粗筛: 预测位置的距离
                pos_a = self._predicted_position(a)
                pos_b = self._predicted_position(b)
                dist_sq = (pos_a - pos_b).length_sq()
                if dist_sq < self._cell_size * self._cell_size:
                    candidates.append((a, b))
        return candidates

    # ── Narrowphase ──

    def _narrowphase(self, candidates: List[Tuple[MotionRequest, MotionRequest]],
                     requests: List[MotionRequest]) -> List[CollisionEvent]:
        """
        Narrowphase — per-pair 精确碰撞检测

        CyberRT来源: AABB/OBB exact test
        PRD #1452: ≤4ms budget

        当前实现: 球体碰撞检测(radius = cell_size/4)
        后续: 集成 models.py 的 Dimensions 做 AABB
        """
        collisions = []
        radius = self._cell_size / 4.0

        for a, b in candidates:
            pos_a = self._predicted_position(a)
            pos_b = self._predicted_position(b)
            diff = pos_a - pos_b
            dist_sq = diff.length_sq()
            min_dist = 2 * radius

            if dist_sq < min_dist * min_dist and dist_sq > 1e-10:
                dist = dist_sq ** 0.5
                normal = Vec3(diff.x / dist, diff.y / dist, diff.z / dist)
                contact = Vec3(
                    (pos_a.x + pos_b.x) / 2,
                    (pos_a.y + pos_b.y) / 2,
                    (pos_a.z + pos_b.z) / 2,
                )
                collisions.append(CollisionEvent(
                    individual_a=a.individual_id,
                    individual_b=b.individual_id,
                    contact_point=contact,
                    contact_normal=normal,
                    penetration_depth=min_dist - dist,
                ))

        return collisions

    # ── Constraint Solve ──

    def _constraint_solve(self, requests: List[MotionRequest],
                          collisions: List[CollisionEvent],
                          dt: float) -> Dict[int, Tuple[Vec3, Quat, int]]:
        """
        约束求解 — 碰撞响应 + 位置校正

        CyberRT来源: world::data::ConstraintSolver (Gauss-Seidel迭代)
        governance对应: 策略规则匹配后的最终裁定

        PRD #1452 关键差异:
          CyberRT: 物理约束求解 — 确定性(相同输入→相同输出)
          governance: 策略规则匹配 — 取决于策略版本

        返回: {individual_id: (position, rotation, resolve_flags)}
        """
        # 计算每个个体的预测位置
        predicted: Dict[int, Vec3] = {}
        rotations: Dict[int, Quat] = {}
        for req in requests:
            predicted[req.individual_id] = self._predicted_position(req)
            rotations[req.individual_id] = req.delta_rotation

        # 碰撞个体集
        collided_ids = set()
        for col in collisions:
            collided_ids.add(col.individual_a)
            collided_ids.add(col.individual_b)

        # Gauss-Seidel 位置校正迭代
        for _ in range(self._solver_iterations):
            for col in collisions:
                a_id, b_id = col.individual_a, col.individual_b
                if a_id not in predicted or b_id not in predicted:
                    continue

                pos_a = predicted[a_id]
                pos_b = predicted[b_id]
                diff = pos_a - pos_b
                dist_sq = diff.length_sq()
                if dist_sq < 1e-10:
                    continue

                dist = dist_sq ** 0.5
                min_dist = self._cell_size / 2.0
                if dist >= min_dist:
                    continue

                # 按 priority 分配修正量
                req_a = next((r for r in requests if r.individual_id == a_id), None)
                req_b = next((r for r in requests if r.individual_id == b_id), None)
                pa = req_a.priority if req_a else 0.0
                pb = req_b.priority if req_b else 0.0
                total = pa + pb + 1e-6
                ratio_a = pb / total  # 低priority的个体被推开更多
                ratio_b = pa / total

                penetration = min_dist - dist
                normal = Vec3(diff.x / dist, diff.y / dist, diff.z / dist)
                correction = penetration * 0.8  # 松弛因子

                predicted[a_id] = pos_a + normal * (correction * ratio_a)
                predicted[b_id] = pos_b - normal * (correction * ratio_b)

        # 构建结果
        result: Dict[int, Tuple[Vec3, Quat, int]] = {}
        for req in requests:
            iid = req.individual_id
            flags = ResolveFlag.NONE
            if iid in collided_ids:
                flags |= ResolveFlag.WAS_COLLIDED
                # 检查是否被修正
                orig = self._predicted_position(req)
                final = predicted[iid]
                if (final - orig).length_sq() > 1e-6:
                    flags |= ResolveFlag.WAS_CORRECTED

            result[iid] = (predicted[iid], rotations.get(iid, Quat()), flags)

        return result

    # ── Commit ──

    def _commit(self, resolved: Dict[int, Tuple[Vec3, Quat, int]],
                tick_seq: int) -> List[ConfirmedState]:
        """
        Phase 2d: State Commit — 发布 ConfirmedState

        CyberRT语义: 只有 WorldResolver 有权发布 ConfirmedState
        governance语义: 只有 ApprovalCoordinator 有权裁定 ExecutionDecision

        ConfirmedState 一旦发布就是世界事实(不可逆)
        """
        confirmed_list = []
        for iid, (pos, rot, flags) in resolved.items():
            state = ConfirmedState(
                individual_id=iid,
                tick_seq=tick_seq,
                position=pos,
                rotation=rot,
                resolve_flags=flags,
            )
            # 更新世界权威状态
            self._confirmed_states[iid] = state
            # 发布
            self._confirmed_writer.write(state)
            confirmed_list.append(state)

        return confirmed_list

    def _publish_unchanged(self, tick_seq: int) -> List[ConfirmedState]:
        """没有 MotionRequest 时，所有个体保持原位"""
        result = []
        for iid, state in self._confirmed_states.items():
            state.tick_seq = tick_seq
            state.resolve_flags = ResolveFlag.NONE
            self._confirmed_writer.write(state)
            result.append(state)
        return result

    # ── 辅助 ──

    def _predicted_position(self, req: MotionRequest) -> Vec3:
        """计算个体的预测位置 = 当前确认位置 + delta"""
        if req.individual_id in self._confirmed_states:
            base = self._confirmed_states[req.individual_id].position
        else:
            base = Vec3(0.0, 0.0, 0.0)
        return base + req.delta_position

    def register_individual(self, individual_id: int, position: Vec3,
                            rotation: Quat = None):
        """注册个体的初始状态 — 对应 CyberRT Node 的注册"""
        self._confirmed_states[individual_id] = ConfirmedState(
            individual_id=individual_id,
            tick_seq=0,
            position=position,
            rotation=rotation or Quat(),
        )

    def get_confirmed_state(self, individual_id: int) -> Optional[ConfirmedState]:
        """获取个体的当前确认状态"""
        return self._confirmed_states.get(individual_id)
