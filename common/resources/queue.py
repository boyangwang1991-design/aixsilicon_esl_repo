"""BoundedQueue（R07）。

合同（规划 §4.1）：
- depth 默认只计等待项；服务中项另计。模型需要总容量时显式配置。
- 满队列 retry/reject 由上游保留，不丢请求；拒绝或重试不算已接受。
- 自带标准埋点（push/pop 计数、occupancy 积分由 observability 聚合器处理）。
"""
from __future__ import annotations

from collections import deque

from ..base.errors import CapacityError


class BoundedQueue:
    """有限 FIFO 队列（仅等待项计数）。

    - push 在满时抛 CapacityError（调用方负责 retry/reject，不丢请求）。
    - pop 在空时返回 None（不静默吞请求；由调用方判断）。
    """

    def __init__(self, depth: int, name: str = "queue"):
        if depth < 1:
            raise CapacityError(f"{name}: depth 必须 >= 1（收到 {depth}）", field="depth", value=depth)
        self.name = name
        self._depth = depth
        self._items = deque()

    @property
    def depth(self) -> int:
        return self._depth

    def __len__(self) -> int:
        """当前等待项数（不含服务中项）。"""
        return len(self._items)

    def full(self) -> bool:
        return len(self._items) >= self._depth

    def empty(self) -> bool:
        return not self._items

    def push(self, item) -> None:
        if self.full():
            raise CapacityError(f"{self.name}: 队列满（depth={self._depth}，等待项={len(self._items)}）",
                                field="depth", value=self._depth)
        self._items.append(item)

    def pop(self):
        """移除并返回队首；空时返回 None。"""
        if not self._items:
            return None
        return self._items.popleft()

    def peek(self):
        return self._items[0] if self._items else None

    def clear(self) -> None:
        self._items.clear()
