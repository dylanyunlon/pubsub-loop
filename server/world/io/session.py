# SPDX-License-Identifier: Apache-2.0
#
# world/io/session.py
#
# 网络会话与轮询器 — 从 CyberRT cyber/io 移植
#
# CyberRT来源:
#   cyber/io/session.h      → Session (TCP/UDP socket封装)
#   cyber/io/poller.h       → Poller (epoll事件轮询)
#   cyber/io/poll_handler.h → PollHandler (事件处理器)
#   cyber/io/poll_data.h    → PollRequest/PollResponse
#
# 语义:
#   CyberRT的io层提供:
#     1. Session: 对POSIX socket的封装(connect/bind/listen/accept/recv/send)
#     2. Poller: 使用epoll做事件驱动IO
#     3. PollHandler: 当socket可读/可写时的回调
#
#   这是CyberRT分布式部署的基础 — 让多个进程/机器上的
#   Component通过网络通信。
#
#   pubsub-loop:
#     单进程内使用IntraTransport(零拷贝)。
#     跨进程使用ShmTransport(共享内存)。
#     网络传输(TCP/UDP)用于分布式世界引擎部署。
#
# governance对应:
#   Session ≈ AgentSession (agent与治理层的通信通道)
#   Poller ≈ EventLoop (事件驱动的策略评估)

import select
import socket
import threading
from dataclasses import dataclass
from enum import IntFlag
from typing import Callable, Dict, List, Optional


class PollEvent(IntFlag):
    """轮询事件类型 — 对应 epoll 事件"""
    READ   = 1  # EPOLLIN
    WRITE  = 2  # EPOLLOUT
    ERROR  = 4  # EPOLLERR
    HUP    = 8  # EPOLLHUP


@dataclass
class PollRequest:
    """轮询请求"""
    fd: int
    events: PollEvent
    callback: Callable[[PollEvent], None]


class Session:
    """
    网络会话 — socket封装

    CyberRT来源 (session.h):
      Socket(domain, type, protocol) → 创建socket
      Bind(addr) / Listen(backlog) → 服务端
      Connect(addr) → 客户端
      Recv/Send → 读写数据
      Close → 关闭

    pubsub-loop使用:
      分布式世界引擎之间的通信:
        - WorldResolver 运行在主节点
        - Individual 可以运行在工作节点
        - MotionRequest/ConfirmedState 通过TCP传输

    使用:
      # 服务端
      server = Session()
      server.bind(("0.0.0.0", 8080))
      server.listen(128)
      client = server.accept()
      data = client.recv(4096)

      # 客户端
      session = Session()
      session.connect(("localhost", 8080))
      session.send(b"motion_request...")
    """

    def __init__(self, sock: socket.socket = None):
        self._sock = sock
        self._fd = sock.fileno() if sock else -1
        self._closed = False

    @classmethod
    def create(cls, family=socket.AF_INET,
               sock_type=socket.SOCK_STREAM,
               proto=0) -> 'Session':
        """创建socket — CyberRT: Session::Socket()"""
        sock = socket.socket(family, sock_type, proto)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.setblocking(False)
        return cls(sock)

    def bind(self, address: tuple):
        """绑定地址 — CyberRT: Session::Bind()"""
        self._sock.bind(address)

    def listen(self, backlog: int = 128):
        """监听 — CyberRT: Session::Listen()"""
        self._sock.listen(backlog)

    def accept(self) -> Optional['Session']:
        """接受连接 — CyberRT: Session::Accept()"""
        try:
            conn, addr = self._sock.accept()
            conn.setblocking(False)
            return Session(conn)
        except BlockingIOError:
            return None

    def connect(self, address: tuple) -> bool:
        """连接 — CyberRT: Session::Connect()"""
        try:
            self._sock.connect(address)
            return True
        except BlockingIOError:
            return True  # 非阻塞connect, 后续通过select检查
        except Exception:
            return False

    def recv(self, bufsize: int = 4096, timeout_ms: int = -1) -> Optional[bytes]:
        """
        接收数据 — CyberRT: Session::Recv()

        timeout_ms: -1=无限等待, 0=立即返回
        """
        if timeout_ms >= 0:
            ready = select.select([self._sock], [], [],
                                  timeout_ms / 1000.0)
            if not ready[0]:
                return None
        try:
            data = self._sock.recv(bufsize)
            return data if data else None  # 空数据=对端关闭
        except (BlockingIOError, ConnectionResetError):
            return None

    def send(self, data: bytes, timeout_ms: int = -1) -> int:
        """
        发送数据 — CyberRT: Session::Send()
        """
        if timeout_ms >= 0:
            ready = select.select([], [self._sock], [],
                                  timeout_ms / 1000.0)
            if not ready[1]:
                return 0
        try:
            return self._sock.send(data)
        except (BlockingIOError, BrokenPipeError):
            return 0

    def close(self):
        """关闭 — CyberRT: Session::Close()"""
        if not self._closed and self._sock:
            self._closed = True
            self._sock.close()

    @property
    def fd(self) -> int:
        return self._fd

    @property
    def closed(self) -> bool:
        return self._closed

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


class Poller:
    """
    事件轮询器 — epoll封装

    CyberRT来源 (poller.h):
      Register(PollRequest) → 注册fd到epoll
      Unregister(PollRequest) → 取消注册
      Poll(timeout_ms) → 等待事件

    与select.select的区别:
      select: O(n) — 每次轮询遍历所有fd
      epoll:  O(1) — 内核维护就绪队列

    pubsub-loop使用:
      当世界引擎需要处理多个网络连接时(分布式部署),
      Poller管理所有Session的IO事件。

    使用:
      poller = Poller()
      poller.register(session.fd, PollEvent.READ,
                      callback=on_data_received)
      poller.start()  # 启动轮询线程
    """

    def __init__(self, poll_timeout_ms: int = 100):
        self._poll_timeout_ms = poll_timeout_ms
        self._handlers: Dict[int, PollRequest] = {}
        self._lock = threading.Lock()
        self._running = False
        self._thread: Optional[threading.Thread] = None

        # 使用select.poll()或select.select()
        # (epoll是Linux特有的, poll更通用)
        try:
            self._poll = select.poll()
            self._use_poll = True
        except AttributeError:
            self._use_poll = False

    def register(self, fd: int, events: PollEvent,
                 callback: Callable[[PollEvent], None]):
        """注册fd — CyberRT: Poller::Register()"""
        req = PollRequest(fd=fd, events=events, callback=callback)
        with self._lock:
            self._handlers[fd] = req

        if self._use_poll:
            poll_events = 0
            if events & PollEvent.READ:
                poll_events |= select.POLLIN
            if events & PollEvent.WRITE:
                poll_events |= select.POLLOUT
            self._poll.register(fd, poll_events)

    def unregister(self, fd: int):
        """取消注册 — CyberRT: Poller::Unregister()"""
        with self._lock:
            self._handlers.pop(fd, None)
        if self._use_poll:
            try:
                self._poll.unregister(fd)
            except (KeyError, OSError):
                pass

    def poll(self, timeout_ms: int = -1) -> List[tuple]:
        """
        轮询事件 — CyberRT: Poller::Poll()

        返回 [(fd, events), ...]
        """
        if self._use_poll:
            t = timeout_ms if timeout_ms >= 0 else self._poll_timeout_ms
            try:
                events = self._poll.poll(t)
            except (OSError, select.error):
                return []
            result = []
            for fd, ev in events:
                pe = PollEvent(0)
                if ev & select.POLLIN:
                    pe |= PollEvent.READ
                if ev & select.POLLOUT:
                    pe |= PollEvent.WRITE
                if ev & (select.POLLERR | select.POLLNVAL):
                    pe |= PollEvent.ERROR
                if ev & select.POLLHUP:
                    pe |= PollEvent.HUP
                result.append((fd, pe))
            return result
        else:
            # select fallback
            with self._lock:
                fds = list(self._handlers.keys())
            if not fds:
                return []
            # We need actual socket objects for select... skip for now
            return []

    def start(self):
        """启动轮询线程"""
        self._running = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        """停止轮询"""
        self._running = False
        if self._thread:
            self._thread.join(timeout=1.0)

    def _run(self):
        """轮询循环"""
        while self._running:
            events = self.poll(self._poll_timeout_ms)
            for fd, pe in events:
                with self._lock:
                    req = self._handlers.get(fd)
                if req:
                    try:
                        req.callback(pe)
                    except Exception:
                        pass
