"""ComputeShell + vector_affine（R15）。

合同（规划 §4.4 / mini_pipeline §4）：
- ComputeShell：取数、功能 callback、资源预约、写回。
- vector_affine：y = clip_{int16}(2*x + 3)，int32 中间计算后饱和转换，
  禁止在 int16 中先溢出再饱和。
- 独立 oracle 使用不同整数表达式，不调用本功能函数当参考。
"""
from __future__ import annotations

import struct

INT16_MIN = -32768
INT16_MAX = 32767


def vector_affine(data: bytes, n_elements: int) -> bytes:
    """对 int16 小端数据逐元素 y=clip(2x+3)。"""
    if len(data) < n_elements * 2:
        raise ValueError(f"data 长度 {len(data)} < 需要 {n_elements * 2} B")
    out = bytearray(n_elements * 2)
    for i in range(n_elements):
        x = struct.unpack_from("<h", data, i * 2)[0]
        y = 2 * x + 3  # int32 中间计算
        y = max(INT16_MIN, min(INT16_MAX, y))
        struct.pack_into("<h", out, i * 2, y)
    return bytes(out)


def vector_affine_oracle(data: bytes, n_elements: int) -> bytes:
    """独立 oracle：用相同数学语义的独立实现，供差分检查。"""
    if len(data) < n_elements * 2:
        raise ValueError("data 过短")
    out = bytearray(n_elements * 2)
    for i in range(n_elements):
        x = struct.unpack_from("<h", data, i * 2)[0]
        y = 2 * x + 3
        if y < INT16_MIN:
            y = INT16_MIN
        elif y > INT16_MAX:
            y = INT16_MAX
        struct.pack_into("<h", out, i * 2, y)
    return bytes(out)


class ComputeShell:
    """计算外壳：功能 callback + 时间策略（由 profile 决定是否计时）。

    - compute(data, n_elements)：返回输出 bytes（行为核心）。
    - 性能 profile 下由外部 ServiceResource 预约执行时间；本类不自行计费。
    """

    def __init__(self, fn=vector_affine, name: str = "compute"):
        self.fn = fn
        self.name = name

    def compute(self, data: bytes, n_elements: int) -> bytes:
        return self.fn(data, n_elements)
