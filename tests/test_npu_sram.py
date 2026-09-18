"""Independent traffic-accounting and strict configuration tests; no timing oracle reuse."""

import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import npu_explore as e  # noqa: E402
import npu_workloads as w  # noqa: E402


def test_config_defaults_match_cpp():
    source = (
        (ROOT / "models/npu_sram_controller/systemc/include/npu_sram_controller/config.hpp")
        .read_text()
        .split("struct Config {")[1]
    )
    defaults = {}
    for declaration in re.findall(r"(?:unsigned|uint64_t)\s+([^;]+);", source):
        for entry in declaration.split(","):
            match = re.fullmatch(r"\s*(\w+)\s*=\s*(\d+)\s*", entry)
            if match:
                defaults[match[1]] = int(match[2])
    for declaration in re.findall(r"std::string\s+([^;]+);", source):
        for key, value in re.findall(r'(\w+)="([^"]+)"', declaration):
            defaults[key] = value
    assert defaults == e.DEFAULTS


def test_gemm_independent_tensor_byte_formula():
    p = w.gemm()
    m, n, k = p.metadata["shape"]
    meta = p.check()
    assert meta["read_bytes"] == 2 * m * k * ((n + 63) // 64) + 2 * k * n * ((m + 63) // 64)
    assert meta["write_bytes"] == 4 * m * n
    assert p.metadata["local_footprint_bytes"] == 49152
    # Every next output tile waits for the preceding stores using its C buffer.
    assert any(t.op == "C" and t.deps for t in p.tasks)


def test_attention_reuse_formula():
    p = w.attention()
    meta = p.check()
    s, h, d = (meta[x] for x in ("sequence", "heads", "head_dim"))
    assert meta["read_bytes"] == 2 * h * s * d + 4 * h * s * d * ((s + 15) // 16)
    assert meta["write_bytes"] == 2 * h * s * d
    p = w.attention(decode=True)
    meta = p.check()
    s, h, d = (meta[x] for x in ("sequence", "heads", "head_dim"))
    assert meta["read_bytes"] == 2 * h * d + 4 * h * s * d
    assert meta["write_bytes"] == 6 * h * d


def test_dag_reproducibility_and_holdout(tmp_path):
    a = w.embedding(seed=11)
    b = w.embedding(seed=11)
    c = w.embedding(seed=23)
    assert a.tasks == b.tasks and a.tasks != c.tasks
    train = w.suite(tmp_path / "train")
    hold = w.suite(tmp_path / "hold", True, (101, 127, 149, 173, 199))
    assert len(train) == len(hold) == 12
    assert train[0].read_bytes() != hold[0].read_bytes()
    for path in train + hold:
        assert path.with_suffix(".json").exists()


def test_strict_config_and_bad_dag():
    with pytest.raises(ValueError):
        e.resolved({"phantom_bandwidth": 999})
    with pytest.raises(ValueError):
        e.resolved({"banks": True})
    p = w.Plan("bad")
    p.tensor("x", (32,), element=1)
    p.task(0, "R", 0, 128)
    with pytest.raises(ValueError):
        p.check()
    p = w.Plan("bad-dependency")
    p.tensor("x", (128,), element=1)
    p.task(0, "R", 0, 128, deps=[1])
    with pytest.raises(ValueError):
        p.check()
