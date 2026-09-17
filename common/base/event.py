"""EventChannel / TaskGraph（R12）。

合同（规划 §4.1）：
- 完成事件先更新可见状态，再通知消费者；支持先检查状态再等待，避免丢唤醒。
- 事件历史不能被错过（挂起消费者在事件发生后仍可检查到状态）。
- TaskGraph：简单 DAG、依赖、状态；上游失败传播；不默认支持复杂抢占。
"""
from __future__ import annotations

from collections import deque


class EventChannel:
    """简单事件通道：emit 通知订阅者；latest 保留最近事件供迟到订阅者检查。"""

    def __init__(self, name: str = "event"):
        self.name = name
        self._subscribers = []
        self._latest = None

    def subscribe(self, fn) -> None:
        self._subscribers.append(fn)

    def emit(self, event_type: str, correlation_id=None, fields=None) -> None:
        ev = {"type": event_type, "correlation_id": correlation_id, "fields": fields or {}}
        self._latest = ev
        for fn in list(self._subscribers):
            fn(ev)

    def latest(self):
        return self._latest


class TaskGraph:
    """简单任务 DAG：依赖就绪判断、完成传播、失败传播。"""

    def __init__(self):
        self._deps = {}  # task_id -> set(parent task_ids)
        self._done = set()
        self._failed = set()

    def add_dependency(self, task_id: str, depends_on) -> None:
        deps = set()
        if isinstance(depends_on, (list, tuple, set)):
            deps = set(depends_on)
        else:
            deps = {depends_on}
        self._deps.setdefault(task_id, set()).update(deps)

    def ready(self, task_id: str) -> bool:
        """所有依赖已成功完成（且无失败祖先）。"""
        if task_id in self._done or task_id in self._failed:
            return False
        for dep in self._deps.get(task_id, ()):
            if dep in self._failed:
                return False
            if dep not in self._done:
                return False
        return True

    def mark_done(self, task_id: str) -> None:
        self._done.add(task_id)

    def mark_failed(self, task_id: str) -> None:
        self._failed.add(task_id)
        # 失败传播：直接/间接依赖失败任务的子任务永不 ready（由 ready() 判定）

    def done(self) -> bool:
        return self._done

    def any_failed(self) -> bool:
        return bool(self._failed)
