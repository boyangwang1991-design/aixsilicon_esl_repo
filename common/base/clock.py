"""整数 tick 时钟与 time_resolution（R06 基础）。

- 目标时间统一使用整数 tick；time_resolution 声明每 tick 的物理时长。
- 同一 tick 内事件按显式稳定顺序处理（见 scheduler/资源合同），不依赖 hash 序。
"""
from __future__ import annotations


class TickClock:
    """单调递增的整数 tick 时钟。

    - current() 返回当前 tick。
    - advance(n) 推进 n 个 tick（n>=0）。
    - 观察代码不得推进仿真时间或改变仲裁结果（见规划 §5.5）。
    """

    def __init__(self, time_resolution_ns: float = 1.0, start_tick: int = 0):
        if time_resolution_ns <= 0:
            raise ValueError("time_resolution_ns 必须 > 0")
        self._tick = start_tick
        self._resolution_ns = float(time_resolution_ns)

    def current(self) -> int:
        return self._tick

    def advance(self, n: int = 1) -> None:
        if n < 0:
            raise ValueError("advance 必须 >= 0")
        self._tick += n

    def time_ns(self) -> float:
        """当前 tick 换算为物理时长（wall time 单列，不作为目标硬件性能）。"""
        return self._tick * self._resolution_ns

    def ticks_to_ns(self, ticks: int) -> float:
        return ticks * self._resolution_ns
