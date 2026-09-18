"""{{name}}：command_engine 模板生成的薄骨架。

边界：复用 BoundedQueue/ServiceResource；本文件只写命令语义与业务 hook，
不复制 common 实现。生成后须可独立运行 tests/test_{{name}}.py。
"""
from __future__ import annotations

from common.base.errors import CapacityError
from common.resources.queue import BoundedQueue
from common.resources.service import ServiceResource


class CommandEngine:
    """命令引擎：accept → 排队 → execute → complete（薄骨架）。"""

    def __init__(self, queue_depth: int = 4, service_ticks: int = 1):
        self._queue = BoundedQueue(queue_depth, name=f"{__name__}.queue")
        self._service = ServiceResource(capacity=1, queue_depth=queue_depth,
                                        service_ticks_fn=lambda c: service_ticks)
        self._completed = []

    def accept(self, command) -> None:
        """接受命令；队列满抛 CapacityError（调用方保留，不丢请求）。"""
        self._queue.push(command)

    def execute(self, fn=None) -> list:
        """推进服务；fn(cmd) 为业务执行 hook（默认空操作）。"""
        while not self._queue.empty() and self._service.free_slots() > 0:
            self._service.accept(self._queue.pop())
        done = self._service.step(1)
        for cmd in done:
            if fn is not None:
                fn(cmd)
            self._completed.append(cmd)
        return done

    def completed(self) -> list:
        return list(self._completed)
