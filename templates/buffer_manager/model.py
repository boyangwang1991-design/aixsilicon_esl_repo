"""{{name}}：buffer_manager 模板生成的薄骨架。

边界：复用 BufferPool；本文件只写分配/释放策略（如按需返回 handle），
不复制 common 实现。生成后须可独立运行 tests/test_{{name}}.py。
"""
from __future__ import annotations

from common.base.errors import CapacityError
from common.memory.buffer_pool import BufferPool


class BufferManager:
    """buffer 管理器：固定 slot 分配/释放（薄骨架）。"""

    def __init__(self, num_slots: int, slot_bytes: int):
        self._pool = BufferPool(num_slots, slot_bytes, name=f"{__name__}.pool")

    def alloc(self):
        try:
            return self._pool.alloc()
        except CapacityError:
            return None  # 调用方等待/重试

    def free(self, handle) -> None:
        self._pool.free(handle)

    def available(self) -> int:
        return self._pool.available()
