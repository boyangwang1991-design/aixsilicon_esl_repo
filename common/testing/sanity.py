"""R26：SanityChecker — 性能自洽检查。

合同（规划 §5.6/§6）：
- 带宽上界、容量、字节守恒、时间顺序。
- 防止“数字好看但模型错误”。
- 检查结果分项记录（checks），供 run.json/checks.json 输出。
"""
from __future__ import annotations


class SanityChecker:
    def __init__(self, name: str = "sanity"):
        self.name = name
        self.checks = []   # {"name", "status": PASS|FAIL|NOT_RUN, "detail"}

    def check(self, name: str, ok: bool, detail: str = "") -> None:
        self.checks.append({"name": name,
                            "status": "PASS" if ok else "FAIL",
                            "detail": detail})

    def overall(self) -> str:
        if any(c["status"] == "FAIL" for c in self.checks):
            return "FAIL"
        if not self.checks:
            return "NOT_RUN"
        return "PASS"

    # ---- 内置检查 ----
    def check_bytes_conserved(self, metric_id, expected_total: float, actual_total: float) -> None:
        self.check(f"{metric_id}.bytes_conserved",
                   expected_total == actual_total,
                   f"expected={expected_total} actual={actual_total}")

    def check_capacity(self, metric_id: str, value: float, capacity: float) -> None:
        """值不超过容量上界。"""
        self.check(f"{metric_id}.within_capacity", value <= capacity + 1e-9,
                   f"value={value} capacity={capacity}")

    def check_resource_bound(self, name: str, actual_ticks: float, lower_bound_ticks: float) -> None:
        """完成时间不小于资源下界。"""
        self.check(f"{name}.lower_bound", actual_ticks >= lower_bound_ticks - 1e-9,
                   f"actual={actual_ticks} lower_bound={lower_bound_ticks}")

    def check_order(self, name: str, a_time, b_time) -> None:
        """a 完成不晚于 b 开始（依赖顺序）。"""
        self.check(f"{name}.order", a_time is None or b_time is None or a_time <= b_time,
                   f"a_time={a_time} b_time={b_time}")
