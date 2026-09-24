"""Reproducible SystemC mesh build, byte oracle, synthetic BM and exploration."""

from __future__ import annotations

import csv
import hashlib
import importlib.util
import itertools
import json
import math
import os
import subprocess
import sys
import time
from pathlib import Path

import npu_explore as sram_tools
import yaml
from multibank import UniqueLoader

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "models/npu_mesh"
DEFAULTS = dict(
    columns=4,
    rows=2,
    link_bytes=32,
    control_bytes=8,
    vcs_per_vn=1,
    depth=8,
    link_latency=1,
    credit_latency=2,
    router_latency=1,
    packet_bytes=256,
    outstanding=16,
    return_bytes=65536,
    command_depth=8,
    command_latency=2,
    timeout_cycles=100000,
    max_transfer=4096,
    shape_bytes_per_cycle=0,
    shape_burst_bytes=4096,
    shape_context_min=2,
    fragment_window=1,
    dma_window=1,
    split=False,
    centralized=False,
    trace=True,
)
ENDPOINT_DEFAULTS = dict(
    router=0,
    base=0,
    size=65536,
    bytes_per_cycle=32,
    latency=8,
    slots=8,
    backend="ddr",
    read_latency=0,
    write_latency=0,
    turnaround_cycles=0,
    sram={},
    priority_context=64,
    age_cycles=128,
    issue_limit=0,
)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def hashes():
    paths = list(MODEL.rglob("*")) + [
        Path(__file__),
        ROOT / "tools/esl_cli.py",
        ROOT / "tools/mesh_interference.py",
        ROOT / "tools/multibank.py",
        ROOT / "tests/test_mesh.py",
        ROOT / "contracts/npu_mesh_axi_bus.md",
    ]
    paths += list((ROOT / "cmake").glob("*")) + [
        ROOT / "contracts/environment.json",
        ROOT / "services/systemc/include/aix/esl/byte_store.hpp",
        ROOT / "services/systemc/include/aix/esl/storage_access.hpp",
    ]
    paths += list((ROOT / "models/npu_sram_controller").rglob("*"))
    paths += [ROOT / "tools/npu_explore.py"]
    for layer in ["primitives", "infrastructure", "services"]:
        paths += list((ROOT / layer / "systemc/include").rglob("*.hpp"))
    return {
        str(p.relative_to(ROOT)): digest(p)
        for p in sorted(paths)
        if p.is_file() and "__pycache__" not in p.parts and "reports" not in p.parts
    }


def command(argv, output, timeout=180):
    with Path(output).open("w") as log:
        result = subprocess.run(
            list(map(str, argv)), stdout=log, stderr=subprocess.STDOUT, timeout=timeout
        )
    if result.returncode:
        raise RuntimeError(f"command exited {result.returncode}: {output}")


def resolve(raw):
    if not isinstance(raw, dict) or set(raw) - (set(DEFAULTS) | {"endpoints"}):
        raise ValueError("unknown mesh configuration field")
    cfg = DEFAULTS | raw
    for k, default in DEFAULTS.items():
        if type(cfg[k]) is not type(default) or (
            type(default) is int
            and not (0 if k in ["shape_bytes_per_cycle", "shape_context_min"] else 1)
            <= cfg[k]
            <= 2**32 - 1
        ):
            raise ValueError(f"invalid configuration {k}")
    if cfg["columns"] > 16 or cfg["rows"] > 16 or cfg["depth"] > 256 or cfg["vcs_per_vn"] > 4:
        raise ValueError("topology/buffer limit")
    if cfg["link_bytes"] not in [16, 32, 64, 128] or cfg["control_bytes"] not in [4, 8, 16, 32]:
        raise ValueError("unsupported link width")
    if not cfg["packet_bytes"] <= cfg["max_transfer"] <= min(cfg["return_bytes"], 1048576):
        raise ValueError("packet/return reservation")
    if cfg["outstanding"] > 65535 or cfg["return_bytes"] > 1073741824:
        raise ValueError("tracker/return implementation limit")
    if cfg["fragment_window"] > 64 or cfg["dma_window"] > 64:
        raise ValueError("fragment/DMA window limit")
    if cfg["shape_context_min"] > 64 or (
        cfg["shape_bytes_per_cycle"] and cfg["shape_burst_bytes"] < cfg["packet_bytes"]
    ):
        raise ValueError("invalid shaping policy")
    endpoints = raw.get(
        "endpoints",
        [
            ENDPOINT_DEFAULTS | {"router": cfg["columns"] * cfg["rows"] - 1},
            ENDPOINT_DEFAULTS | {"router": 0, "base": 65536},
        ],
    )
    if not isinstance(endpoints, list) or not endpoints:
        raise ValueError("endpoints required")
    cfg["endpoints"] = []
    for raw_endpoint in endpoints:
        if not isinstance(raw_endpoint, dict) or set(raw_endpoint) - set(ENDPOINT_DEFAULTS):
            raise ValueError("unknown endpoint field")
        e = ENDPOINT_DEFAULTS | raw_endpoint
        if any(type(v) is not int or v < 0 for k, v in e.items() if k not in ["backend", "sram"]):
            raise ValueError("endpoint integer fields required")
        if e["backend"] not in ["ddr", "sram"]:
            raise ValueError("unknown memory backend")
        if not isinstance(e["sram"], dict):
            raise ValueError("SRAM config must be a mapping")
        if (
            e["priority_context"] > 64
            or not e["age_cycles"]
            or e["issue_limit"] > 65535
            or (e["backend"] != "sram" and (e["priority_context"] != 64 or e["issue_limit"]))
        ):
            raise ValueError("invalid SRAM entrance QoS policy")
        if e["backend"] == "sram":
            raw_sram = e["sram"]
            if (
                raw_sram.get("full_data", 1) != 1
                or raw_sram.get("capacity", e["size"]) != e["size"]
            ):
                raise ValueError("SRAM full_data/capacity must match endpoint")
            e["sram"] = sram_tools.resolved(raw_sram | {"full_data": 1, "capacity": e["size"]})
            if (
                e["base"] % 4096
                or e["size"] % 128
                or e["sram"]["mapping"] == "region"
                or e["sram"]["scrub_interval"]
            ):
                raise ValueError("SRAM adapter alignment/full-map/no-scrub contract")
        elif e["sram"]:
            raise ValueError("DDR endpoint cannot silently ignore SRAM configuration")
        if not e["size"] or e["size"] > 67108864 or not e["slots"] or not e["bytes_per_cycle"]:
            raise ValueError("endpoint resource limit")
        if e["router"] >= cfg["columns"] * cfg["rows"] or e["base"] + e["size"] > 2**64 - 1:
            raise ValueError("endpoint coordinate/range")
        if any(
            e["base"] < p["base"] + p["size"] and p["base"] < e["base"] + e["size"]
            for p in cfg["endpoints"]
        ):
            raise ValueError("overlapping map")
        cfg["endpoints"].append(e)
    return cfg


def validate_trace(raw, cfg):
    if (
        not isinstance(raw, dict)
        or set(raw) != {"schema_version", "rows"}
        or raw["schema_version"] != "npu_mesh_bm/v1"
    ):
        raise ValueError("BM trace schema_version/fields")
    if not isinstance(raw["rows"], list) or any(not isinstance(r, dict) for r in raw["rows"]):
        raise ValueError("BM rows must be a list of mappings")
    rows, seen = [], set()
    for item in raw["rows"]:
        defaults = dict(
            release=0,
            source=0,
            context=0,
            axi_id=0,
            depends=[],
            destinations=[],
            data="",
            enables="",
            category="bm",
        )
        if set(item) - (set(defaults) | {"id", "op", "address", "bytes"}):
            raise ValueError("unknown BM row field")
        r = defaults | item
        for field in ["id", "release", "source", "context", "axi_id", "address", "bytes"]:
            if type(r.get(field)) is not int or not 0 <= r[field] <= (
                2**64 - 1 if field == "address" else 2**32 - 1
            ):
                raise ValueError(f"invalid BM {field}")
        if r["id"] in seen or r["source"] >= cfg["columns"] * cfg["rows"] or r["context"] >= 64:
            raise ValueError("duplicate identity/invalid ingress")
        if not isinstance(r["depends"], list) or any(
            type(x) is not int or x not in seen for x in r["depends"]
        ):
            raise ValueError("dependency must precede consumer; cyclic/unknown dependency")
        if r["op"] not in ["READ", "WRITE", "COPY", "MULTICAST", "FENCE"]:
            raise ValueError("unsupported BM opcode")
        if (
            not isinstance(r["category"], str)
            or not r["category"]
            or not r["category"].replace("_", "").isalnum()
        ):
            raise ValueError("invalid category")
        if not isinstance(r["destinations"], list) or any(
            type(x) is not int or not 0 <= x < 2**64 for x in r["destinations"]
        ):
            raise ValueError("destination range")
        if (r["op"] == "COPY" and len(r["destinations"]) != 1) or (
            r["op"] == "MULTICAST" and not r["destinations"]
        ):
            raise ValueError("DMA destinations")
        if len(set(r["destinations"])) != len(r["destinations"]):
            raise ValueError("duplicate multicast destination")
        if r["op"] not in ["COPY", "MULTICAST"] and r["destinations"]:
            raise ValueError("destinations not applicable")
        if (
            r["op"] == "FENCE"
            and r["bytes"]
            or r["op"] in ["READ", "WRITE"]
            and not 0 < r["bytes"] <= cfg["max_transfer"]
        ):
            raise ValueError("transfer size")
        if r["op"] == "WRITE" and len(bytes.fromhex(r["data"])) != r["bytes"]:
            raise ValueError("write payload size")
        if r["enables"] and (r["op"] != "WRITE" or len(bytes.fromhex(r["enables"])) != r["bytes"]):
            raise ValueError("byte enable size")
        if r["op"] != "WRITE" and r["data"]:
            raise ValueError("payload not applicable")

        def mapped(a, length=r["bytes"]):
            return any(
                e["base"] <= a and a + length <= e["base"] + e["size"] for e in cfg["endpoints"]
            )

        if r["op"] != "FENCE" and (
            not mapped(r["address"]) or any(not mapped(a) for a in r["destinations"])
        ):
            raise ValueError("unmapped BM range")
        seen.add(r["id"])
        rows.append(r)
    if not rows:
        raise ValueError("empty BM trace")
    return rows


def synthetic(cfg, scenario="mixed", count=64):
    rows = []

    def add(op, address, size, source, depends=None, category="prefill", destinations=None):
        i = len(rows)
        r = dict(
            id=i,
            op=op,
            address=address,
            bytes=size,
            source=source,
            axi_id=i % 8,
            depends=depends or [],
            category=category,
        )
        if op == "WRITE":
            r["data"] = bytes((i * 17 + n) % 256 for n in range(size)).hex()
        if destinations is not None:
            r["destinations"] = destinations
        rows.append(r)
        return i

    endpoints = cfg["endpoints"]
    nodes = cfg["rows"] * cfg["columns"]
    if scenario in ("mixed", "prefill", "decode", "hotspot"):
        # Distinct 512-byte blocks; explicitly dependent reads test real byte correctness.
        if count * 512 > min(e["size"] for e in endpoints):
            raise ValueError("synthetic footprint exceeds endpoint")
        for i in range(count):
            e = endpoints[0 if scenario == "hotspot" else i % len(endpoints)]
            a = e["base"] + i * 512
            if scenario == "decode":
                rd = add("READ", a, 32, (i + 1) % nodes, category="decode")
                rows[rd]["release"] = i * 2
                continue
            w = add("WRITE", a, 256, i % nodes, category="prefill")
            if scenario in ("mixed", "decode"):
                rd = add("READ", a, 32, (i + 1) % nodes, [w], category="decode")
                rows[rd]["release"] = i * 2
    elif scenario in ("kv_migration", "multicast"):
        if len(endpoints) < 2:
            raise ValueError("DMA BM requires two endpoints")
        for i in range(min(count, 32)):
            a = endpoints[0]["base"] + i * 512
            w = add("WRITE", a, 256, i % nodes)
            destinations = [endpoints[1]["base"] + i * 512]
            if scenario == "multicast":
                destinations.append(endpoints[1]["base"] + 32768 + i * 512)
            copy = add(
                "MULTICAST" if scenario == "multicast" else "COPY",
                a,
                256,
                i % nodes,
                [w],
                scenario,
                destinations,
            )
            for d in destinations:
                add("READ", d, 256, (i + 1) % nodes, [copy], scenario)
    elif scenario == "multicast_large":
        if len(endpoints) < 2 or min(e["size"] for e in endpoints) < 65536:
            raise ValueError("large multicast BM needs two 64KB endpoints")
        for job in range(4):
            address = endpoints[0]["base"] + job * 8192
            parents = []
            for offset in range(0, 8192, cfg["max_transfer"]):
                parents.append(
                    add(
                        "WRITE",
                        address + offset,
                        min(cfg["max_transfer"], 8192 - offset),
                        job % nodes,
                        category="initialize",
                    )
                )
            destinations = [
                endpoints[1]["base"] + job * 16384,
                endpoints[1]["base"] + job * 16384 + 8192,
            ]
            copy = add(
                "MULTICAST", address, 8192, job % nodes, parents, "multicast_large", destinations
            )
            for dest in destinations:
                for offset in range(0, 8192, cfg["max_transfer"]):
                    add(
                        "READ",
                        dest + offset,
                        min(cfg["max_transfer"], 8192 - offset),
                        (job + 1) % nodes,
                        [copy],
                        "readback",
                    )
    else:
        raise ValueError("unknown synthetic scenario")
    return {"schema_version": "npu_mesh_bm/v1", "rows": rows}


def build(output):
    build_dir = output / "build"
    prefix = os.environ.get("SYSTEMC_HOME", str(Path.home() / ".local/systemc-3.0.2"))
    command(
        [
            "cmake",
            "-S",
            MODEL,
            "-B",
            build_dir,
            f"-DCMAKE_PREFIX_PATH={prefix}",
            "-DESL_MESH_BUILD_TESTS=ON",
            "-DESL_NPU_BUILD_TESTS=ON",
            "-DESL_MESH_BUILD_RUNNER=ON",
        ],
        output / "configure.log",
    )
    command(["cmake", "--build", build_dir, "-j", "4"], output / "build.log")
    return build_dir


def quantiles(values):
    values = sorted(values)
    return {f"p{q}": values[max(0, math.ceil(q / 100 * len(values)) - 1)] for q in [50, 95, 99]} | {
        "max": max(values)
    }


def run(cfg, raw, output, executable):
    output.mkdir(parents=True, exist_ok=False)
    rows = validate_trace(raw, cfg)
    spec = importlib.util.spec_from_file_location(
        "mesh_oracle", MODEL / "reference/python/oracle.py"
    )
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    expected, memory = oracle.evaluate(cfg, rows)
    (output / "config.json").write_text(json.dumps(cfg, indent=2))
    (output / "workload.json").write_text(json.dumps(raw, indent=2))
    with (output / "config.txt").open("w") as f:
        for k, v in cfg.items():
            if k != "endpoints":
                f.write(f"{k} {int(v)}\n")
        for index, e in enumerate(cfg["endpoints"]):
            sram_path = output / f"sram-{index}.cfg"
            if e["backend"] == "sram":
                sram_tools.write_config(e["sram"], sram_path)
            keys = [
                "router",
                "base",
                "size",
                "bytes_per_cycle",
                "latency",
                "slots",
                "backend",
                "read_latency",
                "write_latency",
                "turnaround_cycles",
                "priority_context",
                "age_cycles",
                "issue_limit",
            ]
            f.write(
                "endpoint "
                + " ".join(str(e[k]) for k in keys)
                + " "
                + json.dumps(str(sram_path) if e["backend"] == "sram" else "-")
                + "\n"
            )
    with (output / "workload.tsv").open("w") as f:
        f.write("npu_mesh_trace_v1\n")
        for r in rows:
            f.write(
                " ".join(
                    map(
                        str,
                        [
                            r["id"],
                            r["release"],
                            r["source"],
                            r["context"],
                            r["axi_id"],
                            r["op"],
                            r["address"],
                            r["bytes"],
                            ",".join(map(str, r["depends"])) or "-",
                            ",".join(map(str, r["destinations"])) or "-",
                            r["data"] or "-",
                            r["enables"] or "-",
                            r["category"],
                        ],
                    )
                )
                + "\n"
            )
    start = time.monotonic()
    command(
        [executable, output / "config.txt", output / "workload.tsv", output, 2000000],
        output / "simulation.log",
        120,
    )
    wall = time.monotonic() - start
    actual = list(csv.DictReader((output / "completions.tsv").open(), delimiter="\t"))
    by_id = {int(c["id"]): c for c in actual}
    checks = []

    def check(name, passed):
        checks.append({"name": name, "status": "PASS" if passed else "FAIL"})

    check("exactly_once", len(actual) == len(rows) and set(by_id) == {r["id"] for r in rows})
    check("terminal_success", all(c["status"] == "OK" for c in actual))
    check(
        "release_accept_complete_order",
        all(int(c["release"]) <= int(c["accepted"]) <= int(c["done"]) for c in actual),
    )
    check("read_byte_oracle", all(by_id[i]["data"] == data.hex() for i, data in expected.items()))
    check(
        "final_memory_oracle",
        all((output / f"memory_{i}.bin").read_bytes() == data for i, data in enumerate(memory)),
    )
    check(
        "dependency_visibility",
        all(
            int(by_id[r["id"]]["accepted"]) >= int(by_id[d]["done"])
            for r in rows
            for d in r["depends"]
        ),
    )
    metrics = json.loads((output / "metrics.json").read_text())
    check("tracker_conservation", metrics["accepted"] == metrics["completed"])
    check("buffer_bound", metrics["occupancy_high"] <= cfg["depth"])
    check("fragment_window_bound", metrics["fragment_peak"] <= cfg["fragment_window"])
    check("read_return_reservation_bound", metrics["return_reserved_peak"] <= cfg["return_bytes"])
    target_metrics = []
    for i, e in enumerate(cfg["endpoints"]):
        target = json.loads((output / f"target-{i}.json").read_text())
        target_metrics.append(target)
        if e["backend"] == "sram":
            check(
                f"sram_{i}_native_conservation",
                target["accepted"] == target["completed"]
                and target["fragments"] == target["fragments_done"],
            )
        else:
            check(f"ddr_{i}_queue_bound", target["queue_peak"] <= e["slots"])
    expected_shaped = sum(
        r["bytes"] * (1 + len(r["destinations"]) if r["op"] in ["COPY", "MULTICAST"] else 1)
        for r in rows
        if cfg["shape_bytes_per_cycle"] and r["context"] >= cfg["shape_context_min"]
    )
    check("shaped_logical_bytes", metrics["shaped_bytes"] == expected_shaped)
    if cfg["shape_bytes_per_cycle"] and cfg["trace"]:
        buckets = {}
        bounded = True
        debited = 0
        with (output / "trace.tsv").open() as f:
            for event in csv.DictReader(f, delimiter="\t"):
                if (
                    event["event"] != "INJECTED_FRAGMENT"
                    or int(event["context"]) < cfg["shape_context_min"]
                ):
                    continue
                key = (event["source"], event["context"])
                cycle, size = int(event["cycle"]), int(event["bytes"])
                tokens, last = buckets.get(key, (cfg["shape_burst_bytes"], cycle))
                tokens = min(
                    cfg["shape_burst_bytes"], tokens + (cycle - last) * cfg["shape_bytes_per_cycle"]
                )
                bounded &= tokens >= size
                buckets[key] = (tokens - size, cycle)
                debited += size
        check("per_source_context_token_bucket_bound", bounded and debited == expected_shaped)
    # A mesh has two directed links per adjacency; centralized baseline has one shared bus.
    directed = (
        1
        if cfg["centralized"]
        else 2 * ((cfg["columns"] - 1) * cfg["rows"] + (cfg["rows"] - 1) * cfg["columns"])
    )
    width = cfg["link_bytes"] + (2 * cfg["control_bytes"] if cfg["split"] else 0)
    check("link_capacity_bound", metrics["link_bytes"] <= metrics["cycles"] * directed * width)
    ports = list(csv.DictReader((output / "ports.tsv").open(), delimiter="\t"))
    check(
        "per_port_byte_accounting",
        sum(int(p["bytes"]) for p in ports if int(p["output"]) != 4) == metrics["link_bytes"],
    )
    output_flits = {}
    for p in ports:
        vn = int(p["vn"])
        channel = (0 if vn == 0 else 2 if vn == 3 else 1) if cfg["split"] else 0
        key = (p["router"], p["output"], channel)
        output_flits[key] = output_flits.get(key, 0) + int(p["flits"])
    check(
        "per_physical_output_capacity", all(n <= metrics["cycles"] for n in output_flits.values())
    )
    logical = sum(
        r["bytes"] * (len(r["destinations"]) if r["op"] in ["COPY", "MULTICAST"] else 1)
        for r in rows
    )
    check("completion_bytes", sum(int(c["bytes"]) for c in actual) == logical)
    check("target_byte_conservation", metrics["target_bytes"] == metrics["useful_bytes"])
    check(
        "dma_reads_each_source_once",
        metrics["dma_source_bytes"]
        == sum(r["bytes"] for r in rows if r["op"] in ["COPY", "MULTICAST"]),
    )
    latency = {
        category: quantiles(
            [int(c["done"]) - int(c["release"]) for c in actual if c["category"] == category]
        )
        for category in sorted({r["category"] for r in rows})
    }
    service = quantiles([int(c["done"]) - int(c["accepted"]) for c in actual])
    result = dict(
        overall_status="PASS" if all(c["status"] == "PASS" for c in checks) else "FAIL",
        execution_status="OK",
        checks=checks,
        backend="SystemC-3.0.2",
        execution_kind="full_data",
        time_unit="1ns_cycle",
        calibration="NOT_CALIBRATED",
        metrics=metrics,
        targets=target_metrics,
        release_to_done=latency,
        accepted_to_done=service,
        host_wall_seconds=wall,
        useful_bytes_per_cycle=metrics["useful_bytes"] / max(1, metrics["cycles"]),
        sources=hashes(),
        input_sha256=digest(output / "workload.json"),
        config_sha256=digest(output / "config.json"),
    )
    (output / "run.json").write_text(json.dumps(result, indent=2))
    if result["overall_status"] != "PASS":
        raise RuntimeError(f"check failure: {output}/run.json")
    return result


def integration(build_dir, output):
    prefix = output / "install"
    moved = output / "relocated"
    command(["cmake", "--install", build_dir, "--prefix", prefix], output / "install.log")
    prefix.rename(moved)
    for mode in ["source", "installed"]:
        consumer = output / f"consumer-{mode}"
        args = ["cmake", "-S", MODEL / "examples/integration", "-B", consumer]
        if mode == "source":
            args.append(f"-DMESH_SOURCE={MODEL}")
        else:
            args.append(
                f"-DCMAKE_PREFIX_PATH={moved};{os.environ.get('SYSTEMC_HOME', str(Path.home() / '.local/systemc-3.0.2'))}"
            )
        command(args, output / f"{mode}-configure.log")
        command(["cmake", "--build", consumer, "-j", "4"], output / f"{mode}-build.log")
        command([consumer / "consumer"], output / f"{mode}-run.log")


def report(points, output):
    """Publish measurements, not a recommendation without calibrated/PPA evidence."""
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    with (output / "sweep.csv").open("w") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "point",
                "width_bits",
                "depth",
                "split",
                "centralized",
                "outstanding",
                "packet_bytes",
                "credit_latency",
                "vcs_per_vn",
                "fragment_window",
                "dma_window",
                "cycles",
                "bytes_per_cycle",
            ]
        )
        for p in points:
            writer.writerow(
                [
                    p["point"],
                    p["link_bytes"] * 8,
                    p["depth"],
                    p["split"],
                    p["centralized"],
                    p["outstanding"],
                    p["packet_bytes"],
                    p["credit_latency"],
                    p["vcs_per_vn"],
                    p["fragment_window"],
                    p["dma_window"],
                    p["cycles"],
                    p["throughput"],
                ]
            )
    figure, axis = plt.subplots(figsize=(10, 4.5))
    axis.bar([str(p["point"]) for p in points], [p["throughput"] for p in points])
    axis.set(
        xlabel="Configuration point (see sweep.csv)",
        ylabel="Logical target bytes / cycle",
        title="Synthetic BM - uncalibrated SystemC resource model",
    )
    figure.tight_layout()
    figure.savefig(output / "throughput.svg")
    plt.close(figure)
    lines = [
        "# Mesh synthetic BM exploration",
        "",
        "全部点使用相同输入；目标资源变更见每点配置。量测含 fill/drain，未做 RTL 或 PPA 校准。",
        "宽窄分离增加物理通道；集中基线为单共享总线，不能声称等面积比较。",
        "",
        "| point | width(bit) | depth | split | centralized | fragment_window | dma_window | cycles | logical B/cycle |",
        "|---|---|---|---|---|---|---|---|---|",
    ]
    for p in points:
        lines.append(
            f"| {p['point']} | {p['link_bytes'] * 8} | {p['depth']} | {p['split']} | {p['centralized']} | {p['fragment_window']} | {p['dma_window']} | {p['cycles']} | {p['throughput']:.3f} |"
        )
    lines += [
        "",
        "![throughput](throughput.svg)",
        "",
        "每点 run.json 保存 p50/p95/p99/max、源码/配置/输入 hash 与独立检查；ports.tsv 定位 router/output/VN 的阻塞。",
        "同源同 ID顺序、有限片段窗口、memory 服务和有限缓冲共同限制吞吐；参数增大不保证混合流严格单调。",
        "不得从本批合成流量得出真实模型 TTFT/Decode、确定性 QoS 或芯片能耗结论。",
    ]
    (output / "report.md").write_text("\n".join(lines) + "\n")


def main(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    try:
        cfg = resolve(
            yaml.load(args.config.read_text(), Loader=UniqueLoader) if args.config else {}
        )
        b = build(output)
        if args.action == "validate":
            command(["ctest", "--test-dir", b, "--output-on-failure"], output / "ctest.log")
            command(
                [sys.executable, "-m", "pytest", ROOT / "tests/test_mesh.py", "-q"],
                output / "pytest.log",
            )
            integration(b, output)
            for scenario in [
                "mixed",
                "prefill",
                "decode",
                "hotspot",
                "kv_migration",
                "multicast",
                "multicast_large",
            ]:
                run(cfg, synthetic(cfg, scenario, 16), output / scenario, b / "mesh_run")
            status = {
                "overall_status": "PASS",
                "execution_status": "OK",
                "sources": hashes(),
                "checks": [
                    {"name": "systemc_module_cases", "status": "PASS"},
                    {"name": "source_consumer", "status": "PASS"},
                    {"name": "relocated_install_consumer", "status": "PASS"},
                    {"name": "seven_BM_byte_oracles", "status": "PASS"},
                    {"name": "python_negative_and_oracle_tests", "status": "PASS"},
                ],
            }
        elif args.action in ["interference", "qos-explore"]:
            if args.workload:
                raise ValueError(
                    "interference uses a matched generated cohort; --workload unsupported"
                )
            import mesh_interference

            experiment = (
                mesh_interference.qos_experiment
                if args.action == "qos-explore"
                else mesh_interference.experiment
            )
            status = experiment(cfg, output, b / "mesh_run")
        elif args.action == "run":
            raw = (
                json.loads(args.workload.read_text())
                if args.workload
                else synthetic(cfg, args.scenario)
            )
            status = run(cfg, raw, output / "run", b / "mesh_run")
        else:
            raw = (
                json.loads(args.workload.read_text())
                if args.workload
                else synthetic(
                    cfg, "multicast_large" if args.action == "memory-explore" else args.scenario
                )
            )
            points = []
            # One-factor baseline comparisons, then width/depth/split combinations.
            matrix = [
                cfg | dict(link_bytes=w, depth=d, split=s)
                for w, d, s in itertools.product([16, 32, 64], [2, 8], [False, True])
            ]
            matrix += [
                cfg | {"centralized": True},
                cfg | {"outstanding": 4},
                cfg | {"packet_bytes": 64},
                cfg | {"credit_latency": 8},
                cfg | {"vcs_per_vn": 2},
            ]
            matrix += [
                cfg
                | {
                    "endpoints": [
                        e | {"router": (e["router"] + 1) % (cfg["columns"] * cfg["rows"])}
                        for e in cfg["endpoints"]
                    ]
                },
                cfg
                | {
                    "endpoints": [
                        (e | {"bytes_per_cycle": max(1, e["bytes_per_cycle"] // 2)})
                        if e["backend"] == "ddr"
                        else (e | {"sram": e["sram"] | {"bank_ii": 2 * e["sram"]["bank_ii"]}})
                        for e in cfg["endpoints"]
                    ]
                },
                cfg
                | {
                    "endpoints": [
                        e
                        | {
                            "latency": e["latency"] + 32,
                            "read_latency": (e["read_latency"] or e["latency"]) + 32,
                            "write_latency": (e["write_latency"] or e["latency"]) + 32,
                        }
                        if e["backend"] == "ddr"
                        else e
                        | {
                            "sram": e["sram"]
                            | {
                                "read_latency": e["sram"]["read_latency"] + 32,
                                "write_latency": e["sram"]["write_latency"] + 32,
                            }
                        }
                        for e in cfg["endpoints"]
                    ]
                },
            ]
            if args.action == "memory-explore":
                matrix = [
                    cfg | {"fragment_window": fw, "dma_window": dw}
                    for fw, dw in itertools.product([1, 2, 4, 8], [1, 2, 4])
                ]
                for banks in [4, 8, 16]:
                    matrix.append(
                        cfg
                        | {
                            "endpoints": [
                                e
                                | {
                                    "sram": e["sram"]
                                    | {"banks": banks, "groups": min(e["sram"]["groups"], banks)}
                                }
                                if e["backend"] == "sram"
                                else e
                                for e in cfg["endpoints"]
                            ]
                        }
                    )
                matrix.append(
                    cfg
                    | {
                        "endpoints": [
                            e | {"sram": e["sram"] | {"dual_port": 1, "dual_ingress": 1}}
                            if e["backend"] == "sram"
                            else e
                            for e in cfg["endpoints"]
                        ]
                    }
                )
                matrix.append(cfg | {"endpoints": [e | {"slots": 2} for e in cfg["endpoints"]]})
                matrix.append(
                    cfg
                    | {
                        "endpoints": [
                            e | {"turnaround_cycles": 16} if e["backend"] == "ddr" else e
                            for e in cfg["endpoints"]
                        ]
                    }
                )
            for i, c in enumerate(matrix):
                result = run(resolve(c), raw, output / f"point-{i:02}", b / "mesh_run")
                points.append(
                    dict(
                        point=i,
                        link_bytes=c["link_bytes"],
                        depth=c["depth"],
                        split=c["split"],
                        centralized=c["centralized"],
                        outstanding=c["outstanding"],
                        packet_bytes=c["packet_bytes"],
                        credit_latency=c["credit_latency"],
                        vcs_per_vn=c["vcs_per_vn"],
                        fragment_window=c["fragment_window"],
                        dma_window=c["dma_window"],
                        memory_endpoints=c["endpoints"],
                        cycles=result["metrics"]["cycles"],
                        throughput=result["useful_bytes_per_cycle"],
                        latency=result["release_to_done"],
                        status=result["overall_status"],
                    )
                )
            status = {
                "overall_status": "PASS",
                "execution_status": "OK",
                "points": points,
                "sources": hashes(),
                "interpretation": "relative uncalibrated resource model; no PPA or real inference prediction",
            }
            report(points, output)
        status["status"] = status["overall_status"]
        status["source_sha256"] = status.pop("sources", hashes())
        (output / "checks.json").write_text(json.dumps(status, indent=2))
        print(json.dumps({"status": "PASS", "output": str(output)}, ensure_ascii=False))
        return 0
    except Exception as exc:
        (output / "checks.json").write_text(
            json.dumps(
                {"overall_status": "FAIL", "execution_status": "ERROR", "error": str(exc)}, indent=2
            )
        )
        raise
