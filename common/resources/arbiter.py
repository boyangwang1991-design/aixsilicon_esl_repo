"""Arbiter（R09）：基础仲裁策略。

合同（规划 §4.1）：
- 仲裁只有一个决策 owner：资源端从已接受等待集合选下一项；不能与 Scheduler
  分别重排得出不同顺序。
- 基础 FIFO 与示例的 ready store 优先为可选策略；不把 BoundedQueue 绑定死为
  FIFO 服务顺序。
- 相同时刻稳定排序（确定性），并声明是否可能饥饿。
"""
from __future__ import annotations

from .queue import BoundedQueue


class Arbiter:
    """确定性仲裁器。

    policy:
      - "fifo": 严格 FIFO。
      - "store_priority": store 优先（同优先级内 FIFO）；用于 mini_pipeline 示例。
    结果只决定"选哪一个已接受项"，不决定资源资格（资格由 Scheduler/资源合同判断）。
    """

    def __init__(self, policy: str = "fifo", name: str = "arbiter"):
        if policy not in ("fifo", "store_priority"):
            raise ValueError(f"{name}: 未知 policy {policy}")
        self.name = name
        self.policy = policy

    def select(self, items):
        """从已接受等待集合选择下一项；返回选中项或 None（空集合）。"""
        if not items:
            return None
        if self.policy == "fifo":
            return items[0]
        if self.policy == "store_priority":
            for item in items:
                if getattr(item, "op", None) == "store":
                    return item
            return items[0]
        return items[0]
