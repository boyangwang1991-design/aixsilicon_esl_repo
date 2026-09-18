"""R05：Command/Memory/Event 最小合同。

合同（规划 §4.1）：
- MemoryRequest：地址空间、地址、长度、读写、可选数据/byte enable、request_id/epoch。
- Command：task_id、操作及类型化参数。
- Completion：原始 ID、epoch、状态和错误原因。
- byte enable 省略表示全部有效；非空时与请求长度一致。
- MemoryPort 读在服务完成时取样，写在完成时可见。
- 请求被接受后必须最终终结；retry/rejected 由上游保留。
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional


@dataclass
class MemoryRequest:
    address_space: str
    address: int
    length: int
    read: bool                 # True=读，False=写
    data: Optional[bytes] = None
    byte_enable: Optional[bytes] = None
    request_id: str = ""
    epoch: int = 0

    def __post_init__(self):
        if self.length <= 0:
            raise ValueError("length 必须 > 0")
        if self.byte_enable is not None and len(self.byte_enable) != self.length:
            raise ValueError("byte_enable 长度须与请求长度一致")


@dataclass
class Command:
    task_id: str
    op: str
    params: dict = field(default_factory=dict)
    epoch: int = 0
    status: str = "pending"    # pending | accepted | completed | failed


@dataclass
class Completion:
    original_id: str
    epoch: int
    status: str               # completed | failed | cancelled
    error_reason: str = ""
