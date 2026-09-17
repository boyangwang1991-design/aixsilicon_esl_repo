"""BMU（R16）：包装 BufferPool 的 buffer 管理单元。

合同（规划 §4.4 / mini_pipeline §5）：
- 固定 buffer pool、slot/handle、等待/释放、容量统计。
- 原子获取输入/输出资源（slot 含输入区+输出区），避免两个 chunk 各拿一半。
- 活动 DMA/Compute 仍引用存储时，取消/复位不能立即把该存储分配给新任务。
"""
from __future__ import annotations

from common.base.errors import CapacityError, LifecycleError
from common.memory.buffer_pool import BufferPool


class Bmu:
    """BMU：基于 BufferPool 的固定 slot 分配。

    alloc(): 有空 slot 返回 handle；满池抛 CapacityError（由调用方等待/重试）。
    free(handle): 释放（重复/过期抛 LifecycleError）。
    """

    def __init__(self, num_slots: int, slot_bytes: int, name: str = "bmu"):
        self.name = name
        self._pool = BufferPool(num_slots, slot_bytes, name=f"{name}.pool")

    def alloc(self):
        return self._pool.alloc()

    def free(self, handle) -> None:
        self._pool.free(handle)

    def available(self) -> int:
        return self._pool.available()

    def allocated(self) -> int:
        return self._pool.allocated()

    def peak_allocated(self) -> int:
        """峰值占用（由 observability 基于事件计算；此处返回当前值占位）。"""
        return self._pool.allocated()

    def reset(self) -> None:
        self._pool.reset()
