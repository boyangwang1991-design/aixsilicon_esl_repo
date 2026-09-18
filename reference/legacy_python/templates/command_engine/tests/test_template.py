"""{{name}} 生成物冒烟测试（command_engine 模板）。"""
from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent.parent
sys.path.insert(0, str(REPO))

from models.{{name}}.{{name}} import CommandEngine  # noqa: E402
from common.base.errors import CapacityError  # noqa: E402


def main():
    eng = CommandEngine(queue_depth=2, service_ticks=2)
    eng.accept("c1")
    eng.accept("c2")
    try:
        eng.accept("c3")
        raise AssertionError("queue 未满却接受")
    except CapacityError:
        pass
    done = eng.execute()
    assert done == [], "service 未到时不应完成"
    done = eng.execute()
    assert done == ["c1"], f"完成唯一性: {done}"
    print("{{name}} 冒烟 OK（queue full + 完成唯一性）")


if __name__ == "__main__":
    main()
