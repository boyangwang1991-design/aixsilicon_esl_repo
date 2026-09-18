"""{{name}}：memory_target 模板生成的薄骨架。

边界：复用 ByteStore/MemoryPort；本文件只写地址/容量配置与响应 hook，
不复制 common 实现。生成后须可独立运行 tests/test_{{name}}.py。
"""
from __future__ import annotations

from common.memory.byte_store import ByteStore
from common.memory.memory_port import MemoryPort
from common.transport.contracts import MemoryRequest


class MemoryTarget:
    """内存目标：ByteStore + MemoryPort（薄骨架）。"""

    def __init__(self, size_bytes: int, base: int = 0, queue_depth: int = 4):
        self._store = ByteStore(size_bytes, base=base, name=f"{__name__}.mem")
        self._port = MemoryPort(self._store, queue_depth=queue_depth, name=f"{__name__}.port")

    def submit(self, req: MemoryRequest) -> None:
        self._port.submit(req)

    def step(self, ticks: int = 1) -> list:
        return self._port.step(ticks)

    def read(self, addr: int, length: int) -> bytes:
        return self._store.read(addr, length)
