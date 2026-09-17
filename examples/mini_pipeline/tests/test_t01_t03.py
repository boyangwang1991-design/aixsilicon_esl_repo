"""mini_pipeline T 系列基础测试（T01–T03 解析预期）。

参考 docs/esl_testcase.md §7：
- T01: 1 chunk 单 slot → 76 ticks（load 20 + compute 36 + store 20）
- T02: 64 chunk 单 slot → 4864 ticks，DMA/Compute 不重叠
- T03: 64 chunk 双 slot → 2592 ticks，DMA/Compute 重叠

运行：cd <workflow-root> && PYTHONPATH=repos/aixsilicon_esl_repo uv run --project . python \
      repos/aixsilicon_esl_repo/examples/mini_pipeline/tests/test_t01_t03.py
"""
from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent.parent
sys.path.insert(0, str(REPO))

from common.base.errors import ConfigError  # noqa: E402
from examples.mini_pipeline.system import (  # noqa: E402
    MiniPipeline, CHUNK_ELEMENTS, oracle_output,
)


def run_case(num_slots, n_elements, label, expect_ticks):
    mp = MiniPipeline(num_slots=num_slots, n_elements=n_elements)
    ticks = mp.run()
    ok_data = mp.check_output(n_elements)
    print(f"[{label}] ticks={ticks} expect={expect_ticks} data_ok={ok_data}")
    assert ok_data, f"{label}: 数据校验失败"
    assert ticks == expect_ticks, f"{label}: ticks {ticks} != 预期 {expect_ticks}"
    return ticks


def main():
    # T01: 单 chunk（256 元素）单 slot
    run_case(1, CHUNK_ELEMENTS, "T01", expect_ticks=76)
    # T02: 64 chunk 单 slot
    run_case(1, CHUNK_ELEMENTS * 64, "T02", expect_ticks=4864)
    # T03: 64 chunk 双 slot
    run_case(2, CHUNK_ELEMENTS * 64, "T03", expect_ticks=2592)
    print("ALL T01–T03 PASS")


if __name__ == "__main__":
    main()
