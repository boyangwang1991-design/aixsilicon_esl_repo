"""实例 ID、request/task ID 与 epoch（R06 基础）。

合同要点（规划 §4.1）：
- 所有异步操作至少携带 request_id / task_id / epoch 中适用的字段。
- 复位提升 epoch；旧事务终结前不回收其仍使用的存储，旧完成不得改变新任务。
"""
from __future__ import annotations

import itertools


class IdAllocator:
    """单调递增的 ID 分配器（每实例一个，保证稳定可重放）。"""

    def __init__(self, prefix: str = ""):
        self._prefix = prefix
        self._counter = itertools.count(1)

    def next(self) -> str:
        n = next(self._counter)
        return f"{self._prefix}{n}" if self._prefix else str(n)


class Epoch:
    """纪元：复位时提升，旧 epoch 的事务不得影响新任务。"""

    def __init__(self, start: int = 1):
        self._value = start

    @property
    def value(self) -> int:
        return self._value

    def bump(self) -> int:
        self._value += 1
        return self._value

    def is_current(self, epoch: int) -> bool:
        return epoch == self._value
