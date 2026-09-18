"""{{name}} oracle：独立参考（pipeline_system 模板）。

边界：oracle 用独立表达式，不调用被测模型功能当参考；归 esl_repo。
"""
from __future__ import annotations

import struct


def expected_output(data: bytes, n_elements: int) -> bytes:
    """示例：与 mini_pipeline 相同语义的独立参考（可按需替换）。"""
    out = bytearray(n_elements * 2)
    for i in range(n_elements):
        x = struct.unpack_from("<h", data, i * 2)[0]
        y = max(-32768, min(32767, 2 * x + 3))
        struct.pack_into("<h", out, i * 2, y)
    return bytes(out)
