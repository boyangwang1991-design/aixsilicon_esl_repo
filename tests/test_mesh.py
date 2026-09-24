"""Strict input rejection and independent byte-oracle checks for mesh BM."""

import importlib.util
import sys
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import mesh_explore as mesh  # noqa: E402


def oracle():
    spec = importlib.util.spec_from_file_location(
        "mesh_test_oracle", mesh.MODEL / "reference/python/oracle.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize(
    "change",
    [
        dict(depth=0),
        dict(depth=True),
        dict(bogus=1),
        dict(link_bytes=24),
        dict(rows=0),
        dict(packet_bytes=5000),
        dict(return_bytes=4),
        dict(endpoints=[]),
        dict(endpoints=[dict(base=0), dict(base=1)]),
    ],
)
def test_invalid_config(change):
    with pytest.raises(ValueError):
        mesh.resolve(change)


@pytest.mark.parametrize(
    "scenario", ["mixed", "prefill", "decode", "hotspot", "kv_migration", "multicast"]
)
def test_synthetic_oracle(scenario):
    cfg = mesh.resolve({})
    rows = mesh.validate_trace(mesh.synthetic(cfg, scenario, 8), cfg)
    expected, memory = oracle().evaluate(cfg, rows)
    assert len(memory) == 2
    assert scenario == "decode" or any(any(bank) for bank in memory)
    if scenario != "prefill" and scenario != "hotspot":
        assert expected


def test_oracle_rejects_race():
    cfg = mesh.resolve({})
    raw = {
        "schema_version": "npu_mesh_bm/v1",
        "rows": [
            dict(id=0, op="WRITE", address=0, bytes=2, data="1234"),
            dict(id=1, op="READ", address=0, bytes=2),
        ],
    }
    with pytest.raises(ValueError, match="data race"):
        oracle().evaluate(cfg, mesh.validate_trace(raw, cfg))
    raw["rows"][1]["depends"] = [0]
    expected, stores = oracle().evaluate(cfg, mesh.validate_trace(raw, cfg))
    assert expected[1] == bytes([0x12, 0x34]) and stores[0][:2] == expected[1]


def test_oracle_masks_and_copy():
    cfg = mesh.resolve({})
    raw = {
        "schema_version": "npu_mesh_bm/v1",
        "rows": [
            dict(id=0, op="WRITE", address=3, bytes=4, data="12345678", enables="ff00ff00"),
            dict(id=1, op="COPY", address=3, bytes=4, destinations=[65537], depends=[0]),
            dict(id=2, op="READ", address=65537, bytes=4, depends=[1]),
        ],
    }
    expected, _ = oracle().evaluate(cfg, mesh.validate_trace(raw, cfg))
    assert expected[2] == bytes([0x12, 0, 0x56, 0])


@pytest.mark.parametrize(
    "field,value",
    [
        ("depends", [1]),
        ("source", 8),
        ("context", 64),
        ("bytes", 0),
        ("address", 2**64 - 1),
        ("op", "REDUCE"),
        ("unknown", 1),
        ("data", "abcd"),
        ("enables", "ff"),
    ],
)
def test_invalid_trace(field, value):
    cfg = mesh.resolve({})
    raw = {"schema_version": "npu_mesh_bm/v1", "rows": [dict(id=0, op="READ", address=0, bytes=8)]}
    raw["rows"][0][field] = value
    with pytest.raises(ValueError):
        mesh.validate_trace(raw, cfg)


def test_manifest_defaults():
    manifest = yaml.safe_load((mesh.MODEL / "model.yaml").read_text())
    for name, value in mesh.DEFAULTS.items():
        assert manifest["parameters"][name]["default"] == value


@pytest.mark.parametrize(
    "change",
    [
        dict(fragment_window=0),
        dict(fragment_window=65),
        dict(dma_window=65),
        dict(endpoints=[dict(backend="unknown")]),
        dict(endpoints=[dict(backend="sram", base=1)]),
        dict(endpoints=[dict(backend="sram", sram=dict(full_data=0))]),
        dict(endpoints=[dict(backend="sram", sram=dict(capacity=1024))]),
        dict(endpoints=[dict(backend="sram", sram=dict(scrub_interval=1))]),
        dict(endpoints=[dict(backend="ddr", sram=dict(banks=4))]),
    ],
)
def test_invalid_memory_config(change):
    with pytest.raises(ValueError):
        mesh.resolve(change)


def test_sram_configuration_is_resolved_and_preserved():
    raw = yaml.safe_load((mesh.MODEL / "configs/sram_ddr.yaml").read_text())
    cfg = mesh.resolve(raw)
    assert cfg["endpoints"][1]["sram"]["capacity"] == 65536
    assert cfg["endpoints"][1]["sram"]["full_data"] == 1
    assert cfg["fragment_window"] == 8 and cfg["dma_window"] == 4
    assert mesh.resolve(cfg) == cfg


@pytest.mark.parametrize("prefill,kv", [(False, False), (True, False), (False, True), (True, True)])
def test_interference_matched_cohort_and_oracle(prefill, kv):
    import mesh_interference as interference

    cfg = mesh.resolve(yaml.safe_load((mesh.MODEL / "configs/sram_ddr.yaml").read_text()))
    alone = interference.workload(cfg)
    mixed = interference.workload(cfg, prefill, kv)
    assert [r for r in mixed["rows"] if r["category"] in ["initialize", "decode"]] == alone["rows"]
    expected, memory = oracle().evaluate(cfg, mesh.validate_trace(mixed, cfg))
    assert len(expected) == interference.COUNT
    assert all(any(data) for data in expected.values())
    if kv:
        assert memory[0][16384:49152] == memory[1][32768:65536]


def test_interference_stage_accounting(tmp_path):
    import mesh_interference as interference

    completions = ["id\tcategory\trelease\taccepted\tdone\tbytes", "0\tinitialize\t0\t0\t10\t4096"]
    trace = ["cycle\thandle\tsource\tcontext\tevent"]
    for i in range(interference.COUNT):
        release = interference.START + i * interference.PERIOD
        completions.append(f"{i + 1}\tdecode\t{release}\t{release + 11}\t{release + 28}\t256")
        for offset, event in [
            (11, "ACCEPTED"),
            (13, "INJECTED_FRAGMENT"),
            (16, "TRANSPORT_ACK"),
            (21, "TARGET_VISIBLE"),
            (28, "TASK_DONE"),
        ]:
            trace.append(f"{release + offset}\t{i + 1}\t3\t1\t{event}")
    (tmp_path / "completions.tsv").write_text("\n".join(completions))
    (tmp_path / "trace.tsv").write_text("\n".join(trace))
    (tmp_path / "ports.tsv").write_text(
        "router\toutput\tvn\tcredit_stalls\tallocation_stalls\tendpoint_stalls\n0\t0\t0\t1\t2\t3\n"
    )
    result = {
        "metrics": dict.fromkeys(
            ["cycles", "admission_stalls", "credit_stalls", "allocation_stalls", "endpoint_stalls"],
            0,
        ),
        "targets": [
            {},
            dict.fromkeys(
                ["bank_conflicts", "conflict_wait", "frontend_stall", "rob_stall", "queue_peak"], 0
            ),
        ],
    }
    actual = interference.summarize(tmp_path, result)
    assert actual["latency"]["p99"] == 28
    assert actual["accepted_to_done"]["p99"] == 17
    assert {k: v["mean"] for k, v in actual["stages"].items()} == dict(
        admission=11,
        niu_before_injection=2,
        request_transport=3,
        target_queue_and_service=5,
        response_transport=7,
    )
    assert actual["decode_window_completed"] == interference.COUNT
    assert actual["top_ports"][0]["stall_events"] == 6
    (tmp_path / "trace.tsv").write_text("\n".join(trace[:-1]))
    with pytest.raises(ValueError, match="exactly one fragment"):
        interference.summarize(tmp_path, result)


@pytest.mark.parametrize(
    "change",
    [
        dict(shape_bytes_per_cycle=-1),
        dict(shape_context_min=65),
        dict(shape_bytes_per_cycle=1, shape_burst_bytes=1),
        dict(shape_burst_bytes=0),
        dict(endpoints=[dict(priority_context=1)]),
        dict(endpoints=[dict(issue_limit=4)]),
        dict(endpoints=[dict(backend="sram", age_cycles=0)]),
        dict(endpoints=[dict(backend="sram", priority_context=65)]),
    ],
)
def test_invalid_qos_policy(change):
    with pytest.raises(ValueError):
        mesh.resolve(change)


def test_qos_configuration_roundtrip():
    cfg = mesh.resolve(yaml.safe_load((mesh.MODEL / "configs/sram_ddr.yaml").read_text()))
    cfg["shape_bytes_per_cycle"] = 8
    cfg["shape_burst_bytes"] = 256
    cfg["endpoints"][1].update(priority_context=1, issue_limit=4, age_cycles=32)
    assert mesh.resolve(cfg) == cfg
