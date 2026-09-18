"""R19–R23 观察资产验证（对应 TODO A06–A10）。

运行：cd <workflow-root> && PYTHONPATH=repos/aixsilicon_esl_repo uv run --project . python \
      repos/aixsilicon_esl_repo/common/observability/tests/test_observability.py
"""
from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent.parent
sys.path.insert(0, str(REPO))

from common.observability.aggregator import (  # noqa: E402
    Aggregator, TimeSeries, _percentile, _union_len,
)
from common.observability.core import Observer, TraceRecorder  # noqa: E402


def test_a06_queue_depth_integral():
    """A06：queue 深度 [0,2)=0、[2,5)=2、[5,10)=1 → 均值 1.1、峰值 2。"""
    ts = TimeSeries()
    ts.add_segment(0, 2, 0)
    ts.add_segment(2, 5, 2)
    ts.add_segment(5, 10, 1)
    mean = ts.mean(0, 10)
    peak = ts.peak(0, 10)
    assert abs(mean - 1.1) < 1e-9, f"mean={mean}"
    assert peak == 2, f"peak={peak}"
    print("A06 OK: queue depth integral mean=1.1 peak=2")


def test_a07_utilization():
    """A07：单 server 忙 [2,5)，窗口 [0,10) → busy=3、utilization=0.3。"""
    agg = Aggregator(final_tick=10)
    agg.record_series("svc.busy", 2, 5, 1)   # busy=1 的区间
    u = agg.utilization("svc.busy", capacity=1)
    assert u["busy_ticks"] == 3, u
    assert abs(u["utilization"] - 0.3) < 1e-9, u
    print("A07 OK: utilization=0.3")


def test_a08_overlap():
    """A08：DMA=[0,4)、[6,10)，Compute=[2,8) → 总重叠=4（并集不重复）。"""
    agg = Aggregator(final_tick=10)
    for s, e in [(0, 4), (6, 10)]:
        agg.record_interval("dma", s, e)
    agg.record_interval("compute", 2, 8)
    # 重叠 = |dma ∩ compute|
    dma = agg.overlap_buckets["dma"]
    comp = agg.overlap_buckets["compute"]
    def inter(a, b):
        s, e = max(a[0], b[0]), min(a[1], b[1])
        return (s, e) if e > s else None
    overlaps = [inter(x, y) for x in dma for y in comp]
    overlaps = [x for x in overlaps if x]
    total = _union_len(overlaps)
    assert total == 4, f"overlap={total}"
    print("A08 OK: overlap union=4")


def test_a09_timeline_units():
    """A09：1 ns/tick 的 1000 ticks 事件导出为 1 μs。"""
    tr = TraceRecorder()
    tr.begin_span("dma", ts_tick=0, correlation_id="c1")
    tr.end_span(1000, "ok")
    trace = tr.export_chrome_trace(time_resolution_ns=1.0)
    assert trace[0]["ts"] == 0.0
    assert trace[0]["dur"] == 1000.0 * (1.0 / 1000.0), trace[0]["dur"]  # 1 μs
    print("A09 OK: 1000 ticks @1ns/tick -> 1.0 us dur")


def test_a10_trace_truncation():
    """A10：trace 限制少量事件，显式报告 dropped；counter 独立。"""
    obs = Observer(mode=Observer.MODE_TRACE)
    # 重置 trace 上限
    obs.trace = TraceRecorder(max_events=3)
    obs.counter("done", 1)
    for i in range(10):
        obs.event("e", tick=i)
    assert obs.trace.dropped == 7, obs.trace.dropped
    assert len(obs.trace._events) == 3
    assert obs.registry.counter_get("done") == 1.0  # counter 不受截断影响
    print("A10 OK: dropped=7, counters independent")


def test_latency_percentile():
    """nearest-rank 百分位。"""
    vals = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    assert _percentile(sorted(vals), 50) == 5
    assert _percentile(sorted(vals), 95) == 10
    assert _percentile([], 50) is None
    print("percentile OK")


def main():
    test_a06_queue_depth_integral()
    test_a07_utilization()
    test_a08_overlap()
    test_a09_timeline_units()
    test_a10_trace_truncation()
    test_latency_percentile()
    print("ALL OBSERVABILITY TESTS PASS")


if __name__ == "__main__":
    main()
