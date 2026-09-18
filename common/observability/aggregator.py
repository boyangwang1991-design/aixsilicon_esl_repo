"""R21：指标聚合器。

合同（规划 §5.3）：
- whole_run：最终终结事件和同 tick 清理全部处理后采集；事件计数含最终 tick；
  积分仍按 [start, final_tick)，分母是实际 elapsed ticks，不人为加一个 tick。
- 分段窗口使用半开区间 [start, end)；资源积分按窗口裁剪。
- latency 给 count/min/mean/P50/P95/max（nearest-rank）；仅 1 样本不渲染分布。
- 零时长/无样本用 None，不除零、不输出伪零延迟/无限吞吐。
- queue.mean_depth 用对深度随时间积分 / 窗口时长，不用 push/pop 次数平均。
- overlap_time 用多区间并集长度，不累加 pair 交集。
"""
from __future__ import annotations


def _percentile(sorted_vals, p):
    """nearest-rank 百分位；空列表返回 None。"""
    if not sorted_vals:
        return None
    n = len(sorted_vals)
    rank = max(1, int((p / 100.0) * n))
    if (p / 100.0) * n > int((p / 100.0) * n):
        rank += 1  # ceil
    rank = min(rank, n)
    return sorted_vals[rank - 1]


def _union_len(intervals):
    """区间并集总长度（overlap_time 用，避免重复计时）。"""
    if not intervals:
        return 0.0
    total = 0.0
    cur_s, cur_e = None, None
    for s, e in sorted(intervals, key=lambda x: (x[0], x[1])):
        if cur_s is None:
            cur_s, cur_e = s, e
            continue
        if s <= cur_e:
            cur_e = max(cur_e, e)
        else:
            total += cur_e - cur_s
            cur_s, cur_e = s, e
    total += cur_e - cur_s
    return total


class TimeSeries:
    """分段常量时间序列（queue 深度、busy 等）。"""

    def __init__(self):
        self._segments = []  # (start, end, value)，半开 [start,end)

    def add_segment(self, start, end, value) -> None:
        if end > start:
            self._segments.append((start, end, value))

    def integral(self, win_start=None, win_end=None) -> float:
        total = 0.0
        for s, e, v in self._segments:
            cs, ce = s, e
            if win_start is not None:
                cs = max(cs, win_start)
            if win_end is not None:
                ce = min(ce, win_end)
            if ce > cs:
                total += (ce - cs) * v
        return total

    def peak(self, win_start=None, win_end=None) -> float:
        best = 0.0
        for s, e, v in self._segments:
            cs, ce = s, e
            if win_start is not None:
                cs = max(cs, win_start)
            if win_end is not None:
                ce = min(ce, win_end)
            if ce > cs:
                best = max(best, v)
        return best

    def mean(self, win_start=None, win_end=None) -> float | None:
        total = self.integral(win_start, win_end)
        dur = (win_end if win_end is not None else (self._segments[-1][1] if self._segments else 0)) \
            - (win_start if win_start is not None else (self._segments[0][0] if self._segments else 0))
        if dur <= 0:
            return None
        return total / dur


class Aggregator:
    """从 Observer 的原始事实计算指标（whole_run 语义）。"""

    def __init__(self, final_tick: int):
        """final_tick：最终终结事件清理后的 tick；积分窗口 [0, final_tick)。"""
        self.final_tick = final_tick
        self.latencies = []          # task 完成延迟样本
        self.series = {}             # metric_id -> TimeSeries
        self.counters = {}           # 计数（成功/失败/错误等）
        self.overlap_buckets = {}    # 类别 -> 区间列表（求并集）

    def record_counter(self, metric_id: str, delta: float = 1) -> None:
        self.counters[metric_id] = self.counters.get(metric_id, 0) + delta

    def record_latency(self, value: float) -> None:
        if value is not None:
            self.latencies.append(value)

    def record_series(self, metric_id: str, start: int, end: int, value: float) -> None:
        self.series.setdefault(metric_id, TimeSeries()).add_segment(start, end, value)

    def record_interval(self, bucket: str, start: int, end: int) -> None:
        if end > start:
            self.overlap_buckets.setdefault(bucket, []).append((start, end))

    # ---- 输出 ----
    def latency_summary(self) -> dict:
        if not self.latencies:
            return {"count": 0, "min": None, "mean": None, "p50": None,
                    "p95": None, "max": None, "note": "no samples"}
        sv = sorted(self.latencies)
        n = len(sv)
        return {
            "count": n,
            "min": sv[0],
            "mean": sum(sv) / n,
            "p50": _percentile(sv, 50),
            "p95": _percentile(sv, 95),
            "max": sv[-1],
        }

    def series_summary(self, metric_id: str) -> dict:
        ts = self.series.get(metric_id)
        if ts is None:
            return {"count": 0, "mean": None, "peak": None, "note": "not recorded"}
        integral = ts.integral(0, self.final_tick)
        mean = None
        if self.final_tick > 0:
            mean = integral / self.final_tick
        return {"mean": mean, "peak": ts.peak(0, self.final_tick), "integral": integral}

    def utilization(self, metric_id: str, capacity: float) -> dict:
        """utilization = 占用积分 / (容量 × 窗口时长)。范围 0–1。"""
        ts = self.series.get(metric_id)
        if ts is None or capacity <= 0 or self.final_tick <= 0:
            return {"utilization": None, "busy_ticks": None}
        integral = ts.integral(0, self.final_tick)
        return {"utilization": integral / (capacity * self.final_tick),
                "busy_ticks": integral}

    def overlap_summary(self) -> dict:
        """各类别区间并集总长。"""
        return {k: _union_len(v) for k, v in self.overlap_buckets.items()}

    def to_dict(self) -> dict:
        return {
            "final_tick": self.final_tick,
            "counters": dict(self.counters),
            "latency": self.latency_summary(),
            "series": {k: self.series_summary(k) for k in self.series},
            "overlap": self.overlap_summary(),
        }
