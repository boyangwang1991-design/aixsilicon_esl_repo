"""esl 公共错误类型（R06 基础）。

统一错误分类，便于 Skill 失败恢复：配置错误在运行前指出字段/值/约束；
数据错误输出第一个差异位置/期望/实际/任务 ID；环境缺失标 BLOCKED。
"""
from __future__ import annotations


class EslError(Exception):
    """基础错误。"""


class ConfigError(EslError):
    """配置不合法（字段/值/连接/约束）。运行前应拒绝。"""

    def __init__(self, message: str, *, field: str | None = None, value=None):
        self.field = field
        self.value = value
        super().__init__(message)


class DataError(EslError):
    """数据不一致（oracle 首个差异）。"""

    def __init__(self, message: str, *, task_id: str | None = None,
                 expected=None, actual=None, position=None):
        self.task_id = task_id
        self.expected = expected
        self.actual = actual
        self.position = position
        super().__init__(message)


class CapacityError(EslError):
    """容量/资源违反（队列满、池耗尽、上界突破）。"""

    def __init__(self, message: str, *, field: str | None = None, value=None):
        self.field = field
        self.value = value
        super().__init__(message)


class LifecycleError(EslError):
    """生命周期违规（旧 epoch 污染、重复释放、悬挂引用）。"""

    def __init__(self, message: str, *, field: str | None = None, value=None):
        self.field = field
        self.value = value
        super().__init__(message)


class EnvironmentError(EslError):
    """环境缺失（编译器/SystemC/工具）→ 上游应标 BLOCKED。"""

    def __init__(self, message: str, *, field: str | None = None, value=None):
        self.field = field
        self.value = value
        super().__init__(message)
