# SPDX-License-Identifier: Apache-2.0
#
# world/transport/channel_manager_v2.py
#
# ChannelManager v2 — 集成 transport 层的通道管理器
#
# 变更:
#   v1 (proto/channels.py): 纯进程内回调，Writer/Reader直接调用
#   v2 (本文件): 通过 transport::Transmitter/Receiver 路由，
#     支持 INTRA/SHM/HYBRID 三种传输模式。
#
# CyberRT来源:
#   cyber/node/node_channel_impl.h:
#     CreateWriter<T>(channel, attr) → 创建 Transmitter + 注册到 TopologyManager
#     CreateReader<T>(channel, callback) → 创建 Receiver + 绑定回调
#   transport选择逻辑在 HybridTransmitter 内部。
#
# 向后兼容:
#   v2 ChannelManager 的 create_writer/create_reader 接口与 v1 相同。
#   现有的 WorldResolver/TickDriver 不需要改代码。
#   只需要在初始化时用 ChannelManagerV2 替换 ChannelManager。

from typing import Callable, Dict, List, Optional, Any
from world.proto.channels import TransportMode
from world.transport.transmitter import (
    Transport, Transmitter, Receiver,
    IntraTransmitter, IntraReceiver,
    IntraDispatcher,
)


class WriterV2:
    """
    v2 Writer — 通过 Transmitter 发送

    v1: 直接调用 listener 回调
    v2: 通过 Transmitter.transmit() → transport层 → Receiver.on_new_message()

    接口不变: writer.write(msg) → True/False
    """

    def __init__(self, channel_name: str, transmitter: Transmitter):
        self.channel_name = channel_name
        self._transmitter = transmitter

    def write(self, msg) -> bool:
        return self._transmitter.transmit(msg)


class ReaderV2:
    """
    v2 Reader — 通过 Receiver 接收

    v1: 回调直接被 Writer 调用
    v2: Receiver.on_new_message() → 回调

    接口不变: reader.callback
    """

    def __init__(self, channel_name: str, receiver: Receiver):
        self.channel_name = channel_name
        self._receiver = receiver


class ChannelManagerV2:
    """
    v2 通道管理器 — 集成 transport 层

    向后兼容 v1 接口:
      create_writer(channel_name, msg_type) → Writer
      create_reader(channel_name, msg_type, callback) → Reader

    新增能力:
      1. 支持指定传输模式 (INTRA/SHM/HYBRID)
      2. 通过 transport::Transport 工厂创建 Transmitter/Receiver
      3. 统一管理所有通道的生命周期
      4. 统计信息(每个通道的消息数、延迟)

    使用(向后兼容):
      # 与v1完全相同 — 默认INTRA模式
      cm = ChannelManagerV2()
      writer = cm.create_writer("/world/tick", WorldTick)
      reader = cm.create_reader("/world/tick", WorldTick, callback=on_tick)
      writer.write(tick)  # → on_tick(tick) 被调用

    使用(v2新功能):
      # 指定SHM模式用于跨进程通信
      cm = ChannelManagerV2(default_mode=TransportMode.SHM)
    """

    def __init__(self, default_mode: TransportMode = TransportMode.INTRA):
        self._default_mode = default_mode
        self._transport = Transport.instance()
        self._writers: Dict[str, WriterV2] = {}
        self._readers: Dict[str, List[ReaderV2]] = {}

    def create_writer(self, channel_name: str, msg_type: type,
                      transport: TransportMode = None) -> WriterV2:
        """创建Writer — 向后兼容v1接口

        CyberRT: auto writer = node->CreateWriter<T>(channel_name)
        """
        mode = transport or self._default_mode

        # INTRA模式: 使用进程内分发
        if mode == TransportMode.INTRA:
            tx = IntraTransmitter(channel_name)
            tx.enable()
        else:
            tx = self._transport.create_transmitter(channel_name, mode)

        writer = WriterV2(channel_name, tx)
        self._writers[channel_name] = writer
        return writer

    def create_reader(self, channel_name: str, msg_type: type,
                      callback: Callable = None,
                      transport: TransportMode = None) -> ReaderV2:
        """创建Reader — 向后兼容v1接口

        CyberRT: auto reader = node->CreateReader<T>(channel_name, callback)
        """
        mode = transport or self._default_mode

        if mode == TransportMode.INTRA:
            rx = IntraReceiver(channel_name, callback)
            rx.enable()
        else:
            rx = self._transport.create_receiver(channel_name, callback, mode)

        reader = ReaderV2(channel_name, rx)
        if channel_name not in self._readers:
            self._readers[channel_name] = []
        self._readers[channel_name].append(reader)
        return reader

    def get_writer(self, channel_name: str) -> Optional[WriterV2]:
        return self._writers.get(channel_name)

    def shutdown(self):
        self._transport.shutdown()
