"""R24/R25：ScenarioRunner + 独立 oracle + 故障注入。

合同（规划 §6 / mini_pipeline §9）：
- ScenarioRunner：数据准备、命令注入、停止条件；pass/fail/blocked 与重现配置。
- Oracle/Scoreboard：数据、事件、完成数、容量守恒；首个差异与关联 ID。
- FaultInjector：指定第 N 次访问失败、延迟、取消/reset；可重现故障与清理结果。
- 独立 oracle 不调用被测功能当参考。
"""
from __future__ import annotations

from ..base.errors import DataError


class ScenarioRunner:
    """场景运行器：给定场景配置与 run 函数，返回状态。"""

    def __init__(self, name: str = "scenario"):
        self.name = name
        self.last_run = None

    def run(self, scenario, fn, **kwargs) -> dict:
        """fn(scenario, **kwargs) -> 运行结果 dict（含 ticks/data/状态）。"""
        result = fn(scenario, **kwargs)
        status = "PASS" if result.get("ok") else "FAIL"
        self.last_run = {"scenario": scenario, "result": result, "status": status}
        return self.last_run


class Oracle:
    """独立 oracle：比较期望与实测，输出首个差异。"""

    def __init__(self, name: str = "oracle"):
        self.name = name

    def check(self, expected: bytes, actual: bytes, task_id: str = "") -> None:
        if expected == actual:
            return
        n = min(len(expected), len(actual))
        pos = None
        for i in range(n):
            if expected[i] != actual[i]:
                pos = i
                break
        if pos is None:
            pos = n  # 长度不同
        raise DataError(
            f"{self.name}: 数据不一致 task={task_id}",
            task_id=task_id, position=pos,
            expected=expected[pos:pos + 8] if expected else b"",
            actual=actual[pos:pos + 8] if actual else b"",
        )

    def check_count(self, expected: int, actual: int, what: str) -> None:
        if expected != actual:
            raise DataError(f"{self.name}: {what} 计数不一致 expected={expected} actual={actual}")


class FaultInjector:
    """确定性故障注入：第 N 次访问失败/延迟/取消。"""

    def __init__(self):
        self._faults = {}  # 注入点类型 -> (nth, action)

    def inject(self, at: str, nth: int, action: str) -> None:
        """action: fail | cancel | delay。"""
        self._faults[at] = (nth, action)
        self._counters = {k: 0 for k in self._faults}

    def apply(self, at: str):
        """在第 N 次访问时返回 action；否则返回 None。"""
        if at not in self._faults:
            return None
        nth, action = self._faults[at]
        self._counters[at] = self._counters.get(at, 0) + 1
        if self._counters[at] == nth:
            return action
        return None

    def reset(self) -> None:
        self._faults = {}
        self._counters = {}
