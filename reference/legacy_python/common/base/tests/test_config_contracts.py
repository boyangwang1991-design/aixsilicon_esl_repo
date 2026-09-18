"""R03–R05 配置与合同验证。

运行：cd <workflow-root> && PYTHONPATH=repos/aixsilicon_esl_repo uv run --project . python \
      repos/aixsilicon_esl_repo/common/base/tests/test_config_contracts.py
"""
from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent.parent
sys.path.insert(0, str(REPO))

from common.base.config import (  # noqa: E402
    ModelFactory, validate_experiment, validate_model, validate_system, validate_vlnv,
)
from common.base.errors import ConfigError  # noqa: E402
from common.memory.byte_store import ByteStore  # noqa: E402
from common.memory.memory_port import MemoryPort  # noqa: E402
from common.transport.contracts import Command, Completion, MemoryRequest  # noqa: E402


def test_r03_model_validation():
    good = {
        "schema_version": 1,
        "id": "aixsilicon:esl:vector_affine:0.1.0",
        "factory": "vector_affine",
        "model_kinds": ["behavioral"],
        "profiles": {"functional": {"timing_model": "untimed"}},
    }
    out = validate_model(good)
    assert out["id"] == good["id"]
    # 非法 VLNV 拒绝
    bad = dict(good, id="company:foo:bar:1.0.0")
    try:
        validate_model(bad)
        raise AssertionError("should reject non-aixsilicon VLNV")
    except ConfigError:
        pass
    # profiles 空拒绝
    try:
        validate_model(dict(good, profiles={}))
        raise AssertionError("should reject empty profiles")
    except ConfigError:
        pass
    print("R03 OK")


def test_r03_system_experiment():
    sys_ok = {"instances": {"dma": {"model": "aixsilicon:esl:dma:0.1.0"}},
              "connections": []}
    validate_system(sys_ok, available_models={"aixsilicon:esl:dma:0.1.0"})
    try:
        validate_system({"instances": {}, "connections": []})
        raise AssertionError("should reject empty instances")
    except ConfigError:
        pass
    exp_ok = {"system": "mini_pipeline.yaml", "sweep": {"slot": [1, 2]}}
    validate_experiment(exp_ok)
    print("R03 system/experiment OK")


def test_r04_factory():
    class FakeModel:
        def __init__(self, lanes=8):
            self.lanes = lanes

    f = ModelFactory()
    f.register("fake", FakeModel)
    m = f.create("fake", lanes=4)
    assert m.lanes == 4
    try:
        f.create("unknown_factory")
        raise AssertionError("should reject unknown factory")
    except ConfigError:
        pass
    print("R04 OK")


def test_r05_contracts_and_memport():
    # MemoryRequest byte_enable 校验
    try:
        MemoryRequest("sram", 0, 4, False, b"\x01\x02", byte_enable=b"\x01")
        raise AssertionError("should reject mismatched byte_enable")
    except ValueError:
        pass
    # MemoryPort 写完成时可见（step 后读可见）
    store = ByteStore(64)
    port = MemoryPort(store, name="mp")
    port.submit(MemoryRequest("ext", 0, 2, False, data=b"\xaa\xbb", request_id="w1"))
    done = port.drain()
    assert done[0]["status"] == "completed"
    assert store.read(0, 2) == b"\xaa\xbb"  # 完成时可见
    # 读取样
    port.submit(MemoryRequest("ext", 0, 2, True, request_id="r1"))
    done = port.drain()
    assert done[0]["data"] == b"\xaa\xbb"
    print("R05 OK")


def main():
    test_r03_model_validation()
    test_r03_system_experiment()
    test_r04_factory()
    test_r05_contracts_and_memport()
    print("ALL CONFIG/CONTRACT TESTS PASS")


if __name__ == "__main__":
    main()
