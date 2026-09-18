"""{{name}} 生成物冒烟测试（memory_target 模板）。"""
from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent.parent
sys.path.insert(0, str(REPO))

from models.{{name}}.{{name}} import MemoryTarget  # noqa: E402
from common.transport.contracts import MemoryRequest  # noqa: E402
from common.base.errors import DataError  # noqa: E402


def main():
    mt = MemoryTarget(size_bytes=64)
    mt.submit(MemoryRequest("ext", 0, 2, False, data=b"\xaa\xbb", request_id="w1"))
    done = mt.step(2)
    assert done and done[0]["status"] == "completed"
    assert mt.read(0, 2) == b"\xaa\xbb", "完成时数据可见"
    try:
        mt.read(63, 2)
        raise AssertionError("越界未报错")
    except DataError:
        pass
    print("{{name}} 冒烟 OK（完成可见 + 越界拒绝）")


if __name__ == "__main__":
    main()
