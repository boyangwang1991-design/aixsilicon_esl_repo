"""R27：SweepRunner + RunComparator。

合同（规划 §6/§9）：
- 配置变化后保存所有点，包括非法配置、失败、超时；失败点不能变成耗时 0 或被删除。
- 比较同口径实验：配置 diff、性能差异、功能状态。
- 非法点过滤（配置合法）后逐点运行；每个点保留状态与完整指标。
"""
from __future__ import annotations


class SweepRunner:
    """参数扫描：保留全部点（含失败/非法/超时）。"""

    def __init__(self, name: str = "sweep"):
        self.name = name
        self.points = []

    def run(self, points, run_fn, valid_fn=None, **run_kwargs) -> list:
        """points: list[dict]（每点参数）；valid_fn: 过滤非法点。
        返回并保存每个点的结果 dict。"""
        self.points = []
        for i, params in enumerate(points):
            point = {"index": i, "params": dict(params), "status": "NOT_RUN"}
            if valid_fn is not None and not valid_fn(params):
                point["status"] = "INVALID"
                point["error"] = "configuration rejected"
                self.points.append(point)
                continue
            try:
                result = run_fn(params, **run_kwargs)
                point.update(result)
                point["status"] = result.get("status", "PASS")
            except Exception as exc:  # noqa: BLE001
                point["status"] = "FAIL"
                point["error"] = str(exc)
            self.points.append(point)
        return self.points

    def summary(self) -> dict:
        counts = {}
        for p in self.points:
            counts[p["status"]] = counts.get(p["status"], 0) + 1
        return {"total": len(self.points), "by_status": counts}


class RunComparator:
    """比较同口径实验：配置 diff、性能差异、功能状态。"""

    def __init__(self):
        self.reports = []

    def compare(self, baseline: dict, variant: dict, metrics=("total_ticks", "throughput")) -> dict:
        """baseline/variant 为两 run 的结果 dict。"""
        report = {
            "baseline_status": baseline.get("status"),
            "variant_status": variant.get("status"),
            "config_diff": self._config_diff(baseline.get("config", {}),
                                             variant.get("config", {})),
            "metrics": {},
        }
        for m in metrics:
            b = baseline.get(m)
            v = variant.get(m)
            if b is not None and v is not None and b != 0:
                report["metrics"][m] = {"baseline": b, "variant": v,
                                        "delta": v - b, "ratio": v / b}
            else:
                report["metrics"][m] = {"baseline": b, "variant": v,
                                        "delta": None, "ratio": None}
        self.reports.append(report)
        return report

    @staticmethod
    def _config_diff(a: dict, b: dict) -> dict:
        """返回两个配置的差异字段。"""
        keys = set(a) | set(b)
        diff = {}
        for k in keys:
            if a.get(k) != b.get(k):
                diff[k] = {"baseline": a.get(k), "variant": b.get(k)}
        return diff
