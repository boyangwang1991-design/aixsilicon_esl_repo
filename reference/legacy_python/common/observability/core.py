"""R19：Counter/Gauge/Span/Event 埋点 API 与收集器。

合同（规划 §5.1/§5.2）：
- 模型只报告事实（counter.add / gauge.set / span.begin / span.end / event.emit），
  不自己算一套 KPI。
- 所有目标时间由仿真内核提供；wall time 单列。
- 高基数 task/request ID 放 trace，不作为 Counter 的标签（避免指标标签膨胀）。
- 观察代码不得推进仿真时间或改变仲裁结果。
"""
from __future__ import annotations

import time


class MetricRegistry:
    """指标注册表：统一时间、单位、tag、关联 ID、过滤。"""

    def __init__(self):
        self._counters = {}   # metric_id -> value
        self._gauges = {}     # metric_id -> (value, tick)
        self._events = []     # 事件记录（有限，供 trace）

    def counter_add(self, metric_id: str, delta: float = 1, labels=None) -> None:
        self._counters[metric_id] = self._counters.get(metric_id, 0) + delta

    def counter_get(self, metric_id: str) -> float:
        return self._counters.get(metric_id, 0.0)

    def gauge_set(self, metric_id: str, value: float, tick: int) -> None:
        self._gauges[metric_id] = (value, tick)

    def gauge_get(self, metric_id: str):
        return self._gauges.get(metric_id, (None, None))

    def counters_snapshot(self) -> dict:
        return dict(self._counters)

    def gauges_snapshot(self) -> dict:
        return {k: v[0] for k, v in self._gauges.items()}


class TraceRecorder:
    """trace 记录：span 与事件，带 correlation_id；不参与指标计算。"""

    def __init__(self, max_events: int = 100000):
        self._max = max_events
        self._spans = []       # {name, pid, tid, ts_tick, dur, correlation_id, status}
        self._events = []      # {name, pid, tid, ts_tick, fields}
        self.dropped = 0

    def begin_span(self, name: str, ts_tick: int, correlation_id=None, resource_id=None):
        self._spans.append({"name": name, "ts": ts_tick, "correlation_id": correlation_id,
                            "resource_id": resource_id, "dur": None, "status": None})

    def end_span(self, ts_tick: int, status: str = "ok"):
        """结束最近一个未闭合 span（栈式）。"""
        for span in reversed(self._spans):
            if span["dur"] is None:
                span["dur"] = ts_tick - span["ts"]
                span["status"] = status
                return span
        return None

    def emit_event(self, name: str, ts_tick: int, correlation_id=None, fields=None):
        if len(self._events) >= self._max:
            self.dropped += 1
            return
        self._events.append({"name": name, "ts": ts_tick,
                             "correlation_id": correlation_id, "fields": fields or {}})

    def spans(self):
        return [s for s in self._spans if s["dur"] is not None]

    def export_chrome_trace(self, time_resolution_ns: float) -> list:
        """导出 Chrome Trace JSON（Perfetto 支持）。tick 换算为微秒。"""
        out = []
        pid = 1
        tid = 1
        us_per_tick = time_resolution_ns / 1000.0
        for s in self._spans:
            if s["dur"] is None:
                continue
            out.append({
                "name": s["name"], "pid": pid, "tid": tid,
                "ts": s["ts"] * us_per_tick, "dur": s["dur"] * us_per_tick,
                "ph": "X", "cat": "span",
                "args": {"correlation_id": s.get("correlation_id"),
                         "resource_id": s.get("resource_id"), "status": s.get("status")},
            })
        for e in self._events:
            out.append({
                "name": e["name"], "pid": pid, "tid": tid,
                "ts": e["ts"] * us_per_tick, "ph": "i", "s": "t", "cat": "event",
                "args": {"correlation_id": e.get("correlation_id"), **e.get("fields", {})},
            })
        return out


class Observer:
    """统一观察入口：off / counters / trace 三档。

    - off：不采集指标与 trace，但保留运行状态（调用方仍可 run）。
    - counters：默认；采集指标，不记录 trace。
    - trace：采集指标 + trace；失败时保留最近事件环形缓冲。
    """

    MODE_OFF = "off"
    MODE_COUNTERS = "counters"
    MODE_TRACE = "trace"

    def __init__(self, mode: str = MODE_COUNTERS, wall: bool = False):
        if mode not in (self.MODE_OFF, self.MODE_COUNTERS, self.MODE_TRACE):
            raise ValueError(f"未知观察模式 {mode}")
        self.mode = mode
        self.record_wall = wall
        self.registry = MetricRegistry()
        self.trace = TraceRecorder()
        self._t0_wall = time.time()

    def set_mode(self, mode: str) -> None:
        if mode not in (self.MODE_OFF, self.MODE_COUNTERS, self.MODE_TRACE):
            raise ValueError(f"未知观察模式 {mode}")
        self.mode = mode

    # ---- 指标 ----
    def counter(self, metric_id: str, delta: float = 1, labels=None) -> None:
        if self.mode == self.MODE_OFF:
            return
        self.registry.counter_add(metric_id, delta, labels)

    def gauge(self, metric_id: str, value: float, tick: int) -> None:
        if self.mode == self.MODE_OFF:
            return
        self.registry.gauge_set(metric_id, value, tick)

    # ---- trace ----
    def span_begin(self, name: str, tick: int, correlation_id=None, resource_id=None) -> None:
        if self.mode != self.MODE_TRACE:
            return
        self.trace.begin_span(name, tick, correlation_id, resource_id)

    def span_end(self, tick: int, status: str = "ok") -> None:
        if self.mode != self.MODE_TRACE:
            return
        self.trace.end_span(tick, status)

    def event(self, name: str, tick: int, correlation_id=None, fields=None) -> None:
        if self.mode != self.MODE_TRACE:
            return
        self.trace.emit_event(name, tick, correlation_id, fields)

    def wall_elapsed(self) -> float:
        """宿主运行时间（不作为目标硬件性能）。"""
        return time.time() - self._t0_wall
