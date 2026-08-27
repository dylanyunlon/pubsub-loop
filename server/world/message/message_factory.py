# SPDX-License-Identifier: Apache-2.0
#
# world/message/message_factory.py
#
# 消息工厂与序列化 — 从 CyberRT cyber/message 移植
#
# CyberRT来源:
#   cyber/message/protobuf_factory.h → ProtobufFactory (protobuf消息工厂)
#   cyber/message/message_header.h   → MessageHeader (消息头: 序列号, 时间戳)
#   cyber/message/message_traits.h   → HasSerializer (序列化特征检测)
#   cyber/message/raw_message.h      → RawMessage (原始字节消息)
#   cyber/message/arena_message_wrapper.h → ArenaMessageWrapper (arena内存管理)
#
# 语义:
#   CyberRT使用protobuf做消息序列化:
#     1. ProtobufFactory 注册所有消息类型
#     2. 通过type_name查找并创建消息实例
#     3. MessageHeader 附加元数据(seq, timestamp, src)
#
#   pubsub-loop差异:
#     Python没有protobuf那样的静态类型注册,
#     但我们仍然需要:
#     - 消息类型注册表 (type_name → class)
#     - 序列化/反序列化 (用于SHM transport跨进程)
#     - 消息头 (seq, timestamp, src_id)
#
# governance对应:
#   MessageFactory ≈ EventSchemaRegistry
#   MessageHeader ≈ AuditEvent metadata
#   序列化 ≈ PolicyInput serialization

import json
import struct
import threading
import time
from dataclasses import dataclass, fields, asdict
from typing import Any, Dict, Optional, Type, TypeVar

T = TypeVar('T')


@dataclass
class MessageHeader:
    """
    消息头 — 附加在每个消息上的元数据

    CyberRT来源 (message_header.h):
      seq_num       → 消息序列号 (per channel递增)
      timestamp_ns  → 发送时间戳 (纳秒)
      src_id        → 发送者ID
      channel_name  → 通道名称

    使用:
      header是transport层自动附加的, 用户代码通常不直接操作。
      EventLog使用header中的seq和timestamp做审计。
    """
    seq_num: int = 0
    timestamp_ns: int = 0
    src_id: int = 0
    channel_name: str = ""


class MessageFactory:
    """
    消息工厂 — 消息类型注册与创建

    CyberRT来源 (protobuf_factory.h):
      RegisterMessage(proto_desc) → 注册protobuf类型
      GenerateMessageByType(type_name) → 按类型名创建实例

    pubsub-loop:
      Python dataclass 不需要 proto descriptor,
      直接用 class 本身做注册。

    使用:
      factory = MessageFactory.instance()

      # 注册
      factory.register(MotionRequest)
      factory.register(ConfirmedState)

      # 创建
      msg = factory.create("MotionRequest")

      # 序列化 (用于SHM transport)
      data = factory.serialize(msg)
      msg2 = factory.deserialize("MotionRequest", data)
    """

    _instance = None
    _lock = threading.Lock()

    @classmethod
    def instance(cls) -> 'MessageFactory':
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = cls()
        return cls._instance

    def __init__(self):
        self._registry: Dict[str, Type] = {}
        self._seq_counters: Dict[str, int] = {}  # per-channel序列号
        self._lock = threading.Lock()

    def register(self, msg_type: Type) -> bool:
        """
        注册消息类型

        CyberRT: ProtobufFactory::RegisterMessage(proto_desc)
        """
        type_name = msg_type.__name__
        with self._lock:
            if type_name in self._registry:
                return False
            self._registry[type_name] = msg_type
        return True

    def create(self, type_name: str) -> Optional[Any]:
        """
        按类型名创建消息实例

        CyberRT: ProtobufFactory::GenerateMessageByType(type_name)
        """
        with self._lock:
            msg_type = self._registry.get(type_name)
        if msg_type is None:
            return None
        return msg_type()

    def get_type(self, type_name: str) -> Optional[Type]:
        """获取消息类型"""
        with self._lock:
            return self._registry.get(type_name)

    def list_types(self):
        """列出所有已注册的消息类型"""
        with self._lock:
            return list(self._registry.keys())

    def next_seq(self, channel_name: str) -> int:
        """获取下一个序列号 — per-channel原子递增"""
        with self._lock:
            seq = self._seq_counters.get(channel_name, 0) + 1
            self._seq_counters[channel_name] = seq
        return seq

    def make_header(self, channel_name: str, src_id: int = 0) -> MessageHeader:
        """创建消息头"""
        return MessageHeader(
            seq_num=self.next_seq(channel_name),
            timestamp_ns=time.monotonic_ns(),
            src_id=src_id,
            channel_name=channel_name,
        )

    # ── 序列化 ──
    #
    # CyberRT使用protobuf的SerializeToString/ParseFromString。
    # pubsub-loop的消息是Python dataclass, 用JSON做跨进程序列化。
    #
    # 为什么用JSON而不是pickle:
    #   1. pickle有安全风险(任意代码执行)
    #   2. JSON可读, 便于调试
    #   3. 性能足够(1000个MotionRequest序列化 < 5ms)
    #   4. 与EventLog的JSON存储格式一致

    def serialize(self, msg: Any) -> bytes:
        """
        序列化消息为bytes

        CyberRT: message.SerializeToString()
        """
        type_name = type(msg).__name__
        data = _dataclass_to_dict(msg)
        payload = json.dumps({"_type": type_name, **data})
        return payload.encode('utf-8')

    def deserialize(self, type_name: str, data: bytes) -> Optional[Any]:
        """
        反序列化bytes为消息

        CyberRT: message.ParseFromString(data)
        """
        msg_type = self.get_type(type_name)
        if msg_type is None:
            return None
        payload = json.loads(data.decode('utf-8'))
        payload.pop("_type", None)
        return _dict_to_dataclass(msg_type, payload)


def _dataclass_to_dict(obj: Any) -> dict:
    """
    递归将dataclass转为dict

    处理嵌套dataclass (如Vec3在MotionRequest中)。
    """
    if not hasattr(obj, '__dataclass_fields__'):
        return obj
    result = {}
    for f in fields(obj):
        val = getattr(obj, f.name)
        if hasattr(val, '__dataclass_fields__'):
            result[f.name] = _dataclass_to_dict(val)
        elif isinstance(val, (list, tuple)):
            result[f.name] = [_dataclass_to_dict(v) for v in val]
        else:
            result[f.name] = val
    return result


def _dict_to_dataclass(cls: Type[T], data: dict) -> T:
    """
    递归将dict转为dataclass实例

    处理嵌套类型(通过类型注解推断)。
    Python 3.10+ 中 f.type 可能是字符串, 需要 get_type_hints() 解析。
    """
    import typing
    if not hasattr(cls, '__dataclass_fields__'):
        return data

    # 解析类型注解 (处理字符串annotation)
    try:
        hints = typing.get_type_hints(cls)
    except Exception:
        hints = {}

    kwargs = {}
    for f in fields(cls):
        if f.name not in data:
            continue
        val = data[f.name]
        # 从hints获取实际类型
        ft = hints.get(f.name)
        # 处理Optional[X] → 提取X
        origin = getattr(ft, '__origin__', None)
        if origin is typing.Union:
            args = [a for a in ft.__args__ if a is not type(None)]
            ft = args[0] if len(args) == 1 else None
        # 检查是否为dataclass
        if ft and isinstance(ft, type) and hasattr(ft, '__dataclass_fields__') and isinstance(val, dict):
            kwargs[f.name] = _dict_to_dataclass(ft, val)
        else:
            kwargs[f.name] = val
    return cls(**kwargs)
