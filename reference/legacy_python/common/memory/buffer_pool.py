"""BufferPool（R11）：固定块/slot 分配。

合同（规划 §4.1/§4.4）：
- 原子 slot 分配；满池等待（调用方决定等待或失败，不丢请求）。
- handle 带 generation/epoch；活动访问结束前不复用；重复释放检测。
- 旧 epoch 事务终结前不回收其仍使用的存储。
"""
from __future__ import annotations

from ..base.errors import CapacityError, LifecycleError


class BufferHandle:
    """带 generation 的 slot 句柄。generation 不匹配即为过期。"""

    __slots__ = ("slot_id", "generation")

    def __init__(self, slot_id: int, generation: int):
        self.slot_id = slot_id
        self.generation = generation

    def __repr__(self):
        return f"BufferHandle(slot={self.slot_id}, gen={self.generation})"


class BufferPool:
    """固定 slot 池：每 slot 一个固定大小区域。

    - alloc(): 有空闲 slot 返回 handle；否则抛 CapacityError（由调用方等待/重试）。
    - free(handle): 释放；过期（generation 不匹配）或重复释放抛 LifecycleError。
    - 占用统计：allocated/peak 由 observability 聚合器基于事件计算。
    """

    def __init__(self, num_slots: int, slot_bytes: int, name: str = "buffer_pool"):
        if num_slots < 1:
            raise CapacityError(f"{name}: num_slots 必须 >= 1", field="num_slots", value=num_slots)
        if slot_bytes < 1:
            raise CapacityError(f"{name}: slot_bytes 必须 >= 1", field="slot_bytes", value=slot_bytes)
        self.name = name
        self.num_slots = num_slots
        self.slot_bytes = slot_bytes
        self._free = list(range(num_slots))  # 栈式空闲（确定性）
        self._gen = [1] * num_slots
        self._in_use = 0

    def available(self) -> int:
        return len(self._free)

    def allocated(self) -> int:
        return self._in_use

    def alloc(self) -> BufferHandle:
        if not self._free:
            raise CapacityError(f"{self.name}: 池已耗尽（{self.num_slots} slots 全部占用）",
                                field="num_slots", value=self.num_slots)
        slot = self._free.pop()
        self._in_use += 1
        return BufferHandle(slot, self._gen[slot])

    def free(self, handle: BufferHandle) -> None:
        if not isinstance(handle, BufferHandle):
            raise LifecycleError(f"{self.name}: 非法 handle 类型")
        gen = self._gen[handle.slot_id]
        if handle.generation != gen:
            raise LifecycleError(f"{self.name}: 过期 handle（slot={handle.slot_id} "
                                 f"gen={handle.generation} != 当前 {gen}）")
        if handle.slot_id in self._free:
            raise LifecycleError(f"{self.name}: 重复释放 slot={handle.slot_id}")
        self._free.append(handle.slot_id)
        self._in_use -= 1

    def bump(self, slot_id: int) -> None:
        """提升某 slot 的 generation（复位/重配时调用）。"""
        self._gen[slot_id] += 1

    def reset(self) -> None:
        """复位：清空占用，全部 slot 回到空闲并提升 generation。"""
        self._free = list(range(self.num_slots))
        for i in range(self.num_slots):
            self._gen[i] += 1
        self._in_use = 0
