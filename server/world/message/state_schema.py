# SPDX-License-Identifier: Apache-2.0
#
# world/message/state_schema.py
#
# 个体状态schema定义与编译期验证 — PRD #16, #228
#
# CyberRT来源:
#   cyber/message/message_traits.h → HasSerializer<T> 编译期检查
#   cyber/message/protobuf_factory.h → 类型注册
#   protobuf .proto 文件 → schema定义
#
# PRD映射:
#   #16 → constexpr个体复合状态类型 — 编译期初始化与校验
#   #228 → 最终化消息schema: 锁定spatial字段, metadata, 保留兼容扩展
#
# 解决的问题:
#   个体状态不是一个扁平结构, 而是复合的:
#     IndividualState:
#       .position  → Vec3 (空间维度)
#       .rotation  → Quat (朝向)
#       .velocity  → Vec3 (物理)
#       .metadata  → Dict (自定义属性)
#       .relations → List[int] (与其他个体的关系)
#
#   需要在"编译期"(Python: 类定义时)校验:
#     1. position/rotation 字段必须存在 (spatial维度是必须的)
#     2. 字段类型必须匹配 (position必须是Vec3)
#     3. 向后兼容: 旧版schema的消息可以被新版反序列化
#
# governance对应:
#   control_specification 的 PolicyInput schema:
#     必须包含 agent_id, session_id (必选字段)
#     可选包含 manifest, snapshot (扩展字段)
#   InterventionPointRequest 的 schema 验证

from dataclasses import dataclass, field, fields as dc_fields
from typing import Dict, List, Optional, Any, Type, Set
from enum import Enum, auto


class FieldCategory(Enum):
    """状态字段分类 — PRD #228"""
    SPATIAL    = auto()  # 空间维度: position, rotation (必须)
    PHYSICAL   = auto()  # 物理属性: velocity, mass, force (可选)
    METADATA   = auto()  # 自定义元数据: name, type, tags (可选)
    RELATION   = auto()  # 关系: parent, children, neighbors (可选)
    EXTENSION  = auto()  # 扩展字段: 用户自定义 (可选)


@dataclass
class FieldSpec:
    """字段规格 — 描述一个状态字段"""
    name: str
    field_type: type
    category: FieldCategory
    required: bool = False
    default: Any = None
    doc: str = ""


# ─── 标准状态Schema — PRD #228 最终化 ───
#
# 锁定的spatial字段: 所有个体必须有 position + rotation
# 可选字段: velocity, metadata, relations
# 扩展保留: 用户可以通过 EXTENSION 添加自定义字段

STANDARD_SCHEMA: List[FieldSpec] = [
    # ── 必选: Spatial ──
    FieldSpec("individual_id", int, FieldCategory.SPATIAL,
              required=True, doc="个体唯一标识"),
    FieldSpec("position_x", float, FieldCategory.SPATIAL,
              required=True, default=0.0, doc="位置X"),
    FieldSpec("position_y", float, FieldCategory.SPATIAL,
              required=True, default=0.0, doc="位置Y"),
    FieldSpec("position_z", float, FieldCategory.SPATIAL,
              required=True, default=0.0, doc="位置Z"),
    FieldSpec("rotation_x", float, FieldCategory.SPATIAL,
              required=True, default=0.0, doc="旋转四元数X"),
    FieldSpec("rotation_y", float, FieldCategory.SPATIAL,
              required=True, default=0.0, doc="旋转四元数Y"),
    FieldSpec("rotation_z", float, FieldCategory.SPATIAL,
              required=True, default=0.0, doc="旋转四元数Z"),
    FieldSpec("rotation_w", float, FieldCategory.SPATIAL,
              required=True, default=1.0, doc="旋转四元数W"),
    # ── 可选: Physical ──
    FieldSpec("velocity_x", float, FieldCategory.PHYSICAL,
              default=0.0, doc="速度X"),
    FieldSpec("velocity_y", float, FieldCategory.PHYSICAL,
              default=0.0, doc="速度Y"),
    FieldSpec("velocity_z", float, FieldCategory.PHYSICAL,
              default=0.0, doc="速度Z"),
    FieldSpec("mass", float, FieldCategory.PHYSICAL,
              default=1.0, doc="质量"),
    # ── 可选: Metadata ──
    FieldSpec("name", str, FieldCategory.METADATA,
              default="", doc="个体显示名称"),
    FieldSpec("entity_type", str, FieldCategory.METADATA,
              default="", doc="个体类型标签"),
    FieldSpec("collision_mask", int, FieldCategory.METADATA,
              default=0xFFFFFFFF, doc="碰撞层掩码"),
    # ── 可选: Relation ──
    FieldSpec("parent_id", int, FieldCategory.RELATION,
              default=-1, doc="父个体ID(-1=无父)"),
]


class SchemaValidator:
    """
    状态Schema验证器 — PRD #16 "编译期"校验

    Python没有真正的constexpr, 但可以在类定义时做校验:
      @validate_state_schema
      class MyIndividualState:
          individual_id: int
          position_x: float
          ...

    如果缺少必选字段, 在类定义时(import阶段)就会报错,
    等价于C++的 static_assert 编译期失败。

    governance对应:
      control_specification 的 validate() 方法:
        检查 PolicyInput 是否包含必须字段 (agent_id, session_id)
    """

    def __init__(self, schema: List[FieldSpec] = None):
        self._schema = schema or STANDARD_SCHEMA
        self._required_fields = {
            f.name for f in self._schema if f.required
        }
        self._field_types = {
            f.name: f.field_type for f in self._schema
        }
        self._defaults = {
            f.name: f.default for f in self._schema
            if f.default is not None
        }

    def validate_type(self, state_type: type) -> List[str]:
        """
        验证一个状态类是否符合schema

        返回错误列表(空=合法)。
        在类定义时调用: 等价于 C++ static_assert。
        """
        errors = []

        # 获取类的字段
        if hasattr(state_type, '__dataclass_fields__'):
            type_fields = set(state_type.__dataclass_fields__.keys())
        elif hasattr(state_type, '__annotations__'):
            type_fields = set(state_type.__annotations__.keys())
        else:
            type_fields = set()

        # 检查必选字段
        for req_field in self._required_fields:
            if req_field not in type_fields:
                errors.append(
                    f"Missing required field: '{req_field}' "
                    f"(category: SPATIAL)")

        # 检查字段类型
        annotations = getattr(state_type, '__annotations__', {})
        for fname, expected_type in self._field_types.items():
            if fname in annotations:
                actual_type = annotations[fname]
                if actual_type != expected_type:
                    errors.append(
                        f"Field '{fname}' type mismatch: "
                        f"expected {expected_type.__name__}, "
                        f"got {actual_type.__name__}")

        return errors

    def is_valid(self, state_type: type) -> bool:
        return len(self.validate_type(state_type)) == 0

    def fill_defaults(self, state_dict: dict) -> dict:
        """为缺失的可选字段填充默认值 — 版本兼容用"""
        result = dict(state_dict)
        for fname, default in self._defaults.items():
            if fname not in result:
                result[fname] = default
        return result


def validate_state_schema(cls):
    """
    装饰器 — 在类定义时验证状态schema

    PRD #16: constexpr 等价

    用法:
        @validate_state_schema
        class MyState:
            individual_id: int
            position_x: float
            position_y: float
            position_z: float
            rotation_x: float = 0.0
            rotation_y: float = 0.0
            rotation_z: float = 0.0
            rotation_w: float = 1.0

    如果缺少必选字段, import时立即抛出 TypeError。
    """
    validator = SchemaValidator()
    errors = validator.validate_type(cls)
    if errors:
        raise TypeError(
            f"State schema validation failed for {cls.__name__}:\n"
            + "\n".join(f"  - {e}" for e in errors))
    # 在类上附加schema元信息
    cls._schema_validated = True
    cls._schema_version = "0.2.0"
    return cls


# ─── StateDescriptor — 编译期类型描述 ───

class StateDescriptor:
    """
    状态类型描述符 — 用于 MessageFactory 注册

    描述一个状态类型的序列化布局:
      字段名, 偏移量, 大小, 类型

    PRD #16: 编译期静态初始化
    Python实现: 类定义时构建, 运行时不可变。
    """

    def __init__(self, state_type: type, schema: List[FieldSpec] = None):
        self._type = state_type
        self._type_name = state_type.__name__
        self._fields: List[FieldSpec] = schema or STANDARD_SCHEMA
        self._field_map = {f.name: f for f in self._fields}
        # 冻结: 运行时不可修改
        self._frozen = True

    @property
    def type_name(self) -> str:
        return self._type_name

    @property
    def required_fields(self) -> Set[str]:
        return {f.name for f in self._fields if f.required}

    @property
    def all_fields(self) -> List[str]:
        return [f.name for f in self._fields]

    def has_field(self, name: str) -> bool:
        return name in self._field_map

    def field_type(self, name: str) -> Optional[type]:
        spec = self._field_map.get(name)
        return spec.field_type if spec else None
