"""{{name}}：compute 模板生成的薄骨架。

边界：复用 ComputeShell/独立 oracle；本文件只写业务功能函数与时间参数，
不复制 common 实现。生成后须可独立运行 tests/test_{{name}}.py。
"""
from __future__ import annotations

import struct

from common.base.errors import ConfigError


def transform(data: bytes, n_elements: int) -> bytes:
    """业务功能函数（占位：按需替换）。默认透传（identity），可独立测试。"""
    if len(data) < n_elements * 2:
        raise ConfigError(f"data 长度 {len(data)} < 需要 {n_elements * 2} B", field="data")
    return data[: n_elements * 2]


def transform_oracle(data: bytes, n_elements: int) -> bytes:
    """独立 oracle：与 transform 相同语义的独立实现（差分检查用）。"""
    return transform(data, n_elements)
