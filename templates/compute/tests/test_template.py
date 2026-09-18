"""{{name}} 生成物冒烟测试（compute 模板）。

运行：cd <workflow-root> && PYTHONPATH=repos/aixsilicon_esl_repo uv run --project . python \
      repos/aixsilicon_esl_repo/models/{{name}}/tests/test_{{name}}.py
"""
from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent.parent
sys.path.insert(0, str(REPO))

from models.{{name}}.{{name}} import transform, transform_oracle  # noqa: E402


def main():
    data = bytes(range(256))
    out = transform(data, 4)
    oracle = transform_oracle(data, 4)
    assert out == oracle, "功能与 oracle 不一致"
    assert len(out) == 8, "输出长度错误"
    print(f"{{name}} 冒烟 OK（identity transform 与 oracle 一致）")


if __name__ == "__main__":
    main()
