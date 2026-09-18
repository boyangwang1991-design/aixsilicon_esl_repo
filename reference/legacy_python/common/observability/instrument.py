"""R20：Queue/Resource/Memory/Buffer 自动埋点。

合同（规划 §5.2/§5.3）：
- 组件自带标准埋点（queue occupancy、resource busy、memory bytes），
  模型只补业务事实，不重复实现。
- 观察代码不得推进仿真时间或改变仲裁结果。
"""
from __future__ import annotations


class InstrumentedMixin:
    """给组件附加 Observer 埋点回调。

    - bind(observer, tick_fn)：tick_fn 提供当前 tick（仿真内核持有）。
    - report_queue(queue, prefix)：上报 depth 时间积分与 push/pop 计数。
    - report_service(service, capacity, prefix)：上报 busy 时间积分。
    - report_bytes(prefix, bytes, direction)：上报 memory.bytes。
    """

    def __init__(self):
        self._observer = None
        self._tick_fn = None

    def bind(self, observer, tick_fn=None):
        self._observer = observer
        self._tick_fn = tick_fn or (lambda: 0)

    # ---- queue ----
    def report_queue(self, queue, prefix: str, tick=None) -> None:
        if self._observer is None:
            return
        t = tick if tick is not None else self._tick_fn()
        self._observer.registry.counter_add(f"{prefix}.push", 0)  # 计数由调用方增
        # 深度积分由外部聚合器基于 push/pop 事件重算；这里提供当前深度 gauge
        self._observer.registry.gauge_set(f"{prefix}.depth", len(queue), t)

    # ---- service ----
    def report_service(self, service, capacity: int, prefix: str, tick=None) -> None:
        if self._observer is None:
            return
        t = tick if tick is not None else self._tick_fn()
        self._observer.registry.gauge_set(f"{prefix}.busy", service.busy_count(), t)
        self._observer.registry.gauge_set(f"{prefix}.capacity", capacity, t)

    # ---- bytes ----
    def report_bytes(self, prefix: str, byte_count: int, direction: str = "rd") -> None:
        if self._observer is None:
            return
        self._observer.registry.counter_add(f"{prefix}.{direction}.bytes", byte_count)
