"""{{name}} 生成物冒烟测试（buffer_manager 模板）。"""
from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent.parent
sys.path.insert(0, str(REPO))

from models.{{name}}.{{name}} import BufferManager  # noqa: E402
from common.base.errors import LifecycleError  # noqa: E402


def main():
    mgr = BufferManager(num_slots=1, slot_bytes=64)
    h = mgr.alloc()
    assert mgr.available() == 0, "用尽后应无空闲"
    mgr.free(h)
    assert mgr.available() == 1, "释放后应恢复"
    try:
        mgr.free(h)
        raise AssertionError("重复释放未报错")
    except LifecycleError:
        pass
    print("{{name}} 冒烟 OK（用尽/释放/重复释放检测）")


if __name__ == "__main__":
    main()
