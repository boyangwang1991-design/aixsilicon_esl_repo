"""ServiceResource（R08）：非流水服务资源。

合同（规划 §4.1）：
- 有限容量（并行度）、排队、服务起止与释放；单 server 服务区间不交叠。
- 一段延迟只由一个 owner 计费；本资源不代其他资源收取延迟。
- 自带标准埋点：busy 积分、queue 深度积分（由 observability 聚合器计算）。
"""
from __future__ import annotations

from ..base.errors import CapacityError
from .queue import BoundedQueue


class ServiceResource:
    """非流水服务资源。

    容量 capacity 表示可并行服务数；排队由内部 BoundedQueue 承担。
    - accept(item, service_ticks)：若排队满则抛 CapacityError（调用方保留请求并重试）。
    - 服务在 accept 后由时钟推进器触发完成（step/drain 接口，见本类方法）。
    """

    def __init__(self, capacity: int, queue_depth: int, name: str = "service",
                 service_ticks_fn=None):
        """service_ticks_fn: item -> int，返回该请求的服务时长（默认 1）。"""
        if capacity < 1:
            raise CapacityError(f"{name}: capacity 必须 >= 1", field="capacity", value=capacity)
        self.name = name
        self.capacity = capacity
        self._queue = BoundedQueue(queue_depth, name=f"{name}.queue")
        self._service_ticks_fn = service_ticks_fn or (lambda item: 1)
        self._serving = {}  # item -> remaining_ticks（key 需可 hash）
        self._finish_order = []  # 保证服务完成顺序稳定

    @property
    def queue(self) -> BoundedQueue:
        return self._queue

    def queue_len(self) -> int:
        return len(self._queue)

    def busy_count(self) -> int:
        return len(self._serving)

    def free_slots(self) -> int:
        return self.capacity - len(self._serving)

    def can_accept(self) -> bool:
        return self.free_slots() > 0

    def accept(self, item) -> None:
        """入队；若队列满抛 CapacityError（不丢请求，由调用方保留）。"""
        self._queue.push(item)

    def step(self, clock_ticks: int = 1) -> list:
        """推进 clock_ticks 个 tick：从队列接纳到空槽，服务计时递减，返回本步完成项。"""
        completed = []
        for _ in range(clock_ticks):
            # 1) 空槽接纳队首
            while self.free_slots() > 0 and not self._queue.empty():
                item = self._queue.pop()
                ticks = self._service_ticks_fn(item)
                self._serving[item] = ticks
                self._finish_order.append(item)
            # 2) 服务递减
            for item in list(self._serving):
                self._serving[item] -= 1
                if self._serving[item] <= 0:
                    del self._serving[item]
                    completed.append(item)
        return completed

    def drain(self) -> list:
        """推进直至队列与服务均清空，返回全部完成项（用于 end-of-run 排空）。"""
        completed = []
        while not self._queue.empty() or self._serving:
            completed.extend(self.step(1))
        return completed
