"""Matched Decode cohorts under synthetic Prefill/KV interference."""

from __future__ import annotations

import csv
import hashlib
import json
from collections import defaultdict

import mesh_explore as mesh

START = 4096
COUNT = 128
PERIOD = 64
END = START + COUNT * PERIOD


def workload(cfg, prefill=False, kv=False, pace=1, phase=0):
    if len(cfg["endpoints"]) != 2 or cfg["columns"] * cfg["rows"] < 8:
        raise ValueError("interference experiment requires two endpoints and at least 8 routers")
    ddr, sram = cfg["endpoints"]
    if (
        ddr["backend"] != "ddr"
        or sram["backend"] != "sram"
        or min(ddr["size"], sram["size"]) < 65536
    ):
        raise ValueError("interference experiment requires DDR then SRAM, each >=64KB")
    if cfg["packet_bytes"] < 256 or cfg["max_transfer"] < 4096 or not cfg["trace"]:
        raise ValueError(
            "interference experiment requires single-packet Decode, 4KB transfers and trace"
        )
    rows = []

    def add(**r):
        rows.append(dict(id=len(rows), **r))
        return len(rows) - 1

    init = []
    # Nonzero immutable source data; every case carries identical initialization.
    for base, size in [(sram["base"], 4096), (ddr["base"] + 16384, 32768)]:
        for offset in range(0, size, 4096):
            init.append(
                add(
                    op="WRITE",
                    address=base + offset,
                    bytes=4096,
                    source=7,
                    axi_id=len(init),
                    context=0,
                    category="initialize",
                    data=bytes((offset // 4096 + n * 13 + 1) % 256 for n in range(4096)).hex(),
                )
            )
    for i in range(COUNT):
        add(
            op="READ",
            address=sram["base"] + (i % 16) * 256,
            bytes=256,
            source=3,
            axi_id=i % 8,
            context=1,
            category="decode",
            release=START + i * PERIOD,
            depends=init,
        )
    # Separate source/context and disjoint address regions prevent semantic data races.
    # Recycling a slot has an explicit completion dependency: offered release is open-loop,
    # actual acceptance remains subject to dependencies and model backpressure.
    if prefill:
        previous = {}
        for i, release in enumerate(range(3072 + phase, END, 32 * pace)):
            slot = i % 16
            previous[slot] = add(
                op="WRITE",
                address=sram["base"] + 16384 + slot * 1024,
                bytes=1024,
                source=2,
                axi_id=slot % 8,
                context=2,
                category="prefill",
                release=release,
                depends=init + ([previous[slot]] if slot in previous else []),
                data=bytes((i + n * 7) % 256 for n in range(1024)).hex(),
            )
    if kv:
        previous = {}
        for i, release in enumerate(range(3072 + phase, END, 128 * pace)):
            slot = i % 8
            previous[slot] = add(
                op="COPY",
                address=ddr["base"] + 16384 + slot * 4096,
                bytes=4096,
                destinations=[sram["base"] + 32768 + slot * 4096],
                source=6,
                axi_id=slot,
                context=3,
                category="kv",
                release=release,
                depends=init + ([previous[slot]] if slot in previous else []),
            )
    return dict(schema_version="npu_mesh_bm/v1", rows=rows)


def distribution(values):
    return mesh.quantiles(values) | dict(
        count=len(values), min=min(values), mean=sum(values) / len(values)
    )


def summarize(output, result):
    with (output / "completions.tsv").open() as f:
        completions = list(csv.DictReader(f, delimiter="\t"))
    dec = [c for c in completions if c["category"] == "decode"]
    if (
        len(dec) != COUNT
        or max(int(c["done"]) for c in completions if c["category"] == "initialize") >= 3072
    ):
        raise ValueError("cohort size or initialization isolation failed")
    events = defaultdict(lambda: defaultdict(list))
    with (output / "trace.tsv").open() as f:
        for t in csv.DictReader(f, delimiter="\t"):
            if t["source"] == "3" and t["context"] == "1":
                events[t["handle"]][t["event"]].append(int(t["cycle"]))
    chain = ["ACCEPTED", "INJECTED_FRAGMENT", "TRANSPORT_ACK", "TARGET_VISIBLE", "TASK_DONE"]
    names = [
        "niu_before_injection",
        "request_transport",
        "target_queue_and_service",
        "response_transport",
    ]
    stages = defaultdict(list)
    services = []
    for e in events.values():
        if any(len(e[k]) != 1 for k in chain):
            raise ValueError("Decode stage analysis requires exactly one fragment per request")
        ticks = [e[k][0] for k in chain]
        if ticks != sorted(ticks):
            raise ValueError("nonmonotonic Decode trace")
        services.append(ticks[-1] - ticks[0])
        for name, a, b in zip(names, ticks[:-1], ticks[1:], strict=True):
            stages[name].append(b - a)
    if sorted(services) != sorted(int(c["done"]) - int(c["accepted"]) for c in dec):
        raise ValueError("Decode trace/completion timing mismatch")
    stages["admission"] = [int(c["accepted"]) - int(c["release"]) for c in dec]
    if any(v < 0 for v in stages["admission"]):
        raise ValueError("Decode accepted before release")
    with (output / "ports.tsv").open() as f:
        ports = [{k: int(v) for k, v in p.items()} for p in csv.DictReader(f, delimiter="\t")]
    for p in ports:
        p["stall_events"] = sum(
            p[k] for k in ["credit_stalls", "allocation_stalls", "endpoint_stalls"]
        )
    window_bytes = sum(int(c["bytes"]) for c in dec if int(c["done"]) < END)
    return dict(
        latency=distribution([int(c["done"]) - int(c["release"]) for c in dec]),
        accepted_to_done=distribution(services),
        stages={k: distribution(v) for k, v in stages.items()},
        decode_window_bytes_per_cycle=window_bytes / (END - START),
        decode_window_completed=window_bytes // 256,
        decode_last_done=max(int(c["done"]) for c in dec),
        top_ports=sorted(ports, key=lambda p: p["stall_events"], reverse=True)[:8],
        counters={
            k: result["metrics"][k]
            for k in [
                "cycles",
                "admission_stalls",
                "credit_stalls",
                "allocation_stalls",
                "endpoint_stalls",
            ]
        },
        sram={
            k: result["targets"][1][k]
            for k in [
                "bank_conflicts",
                "conflict_wait",
                "frontend_stall",
                "rob_stall",
                "queue_peak",
            ]
        },
    )


def experiment(cfg, output, executable):
    variants = [
        ("alone", cfg, False, False, 1),
        ("prefill", cfg, True, False, 1),
        ("kv", cfg, False, True, 1),
        ("mixed", cfg, True, True, 1),
        ("mixed_link64", cfg | {"link_bytes": 64}, True, True, 1),
        (
            "mixed_slots32",
            cfg | {"endpoints": [e | {"slots": 32} for e in cfg["endpoints"]]},
            True,
            True,
            1,
        ),
        ("mixed_fragment2", cfg | {"fragment_window": 2}, True, True, 1),
        (
            "mixed_bank_ii1",
            cfg
            | {
                "endpoints": [
                    cfg["endpoints"][0],
                    cfg["endpoints"][1] | {"sram": cfg["endpoints"][1]["sram"] | {"bank_ii": 1}},
                ]
            },
            True,
            True,
            1,
        ),
        (
            "mixed_ddr64",
            cfg
            | {"endpoints": [cfg["endpoints"][0] | {"bytes_per_cycle": 64}, cfg["endpoints"][1]]},
            True,
            True,
            1,
        ),
        ("mixed_background_half_rate", cfg, True, True, 2),
    ]
    points = []
    cohort_hash = None
    variants = [(*v, 0) for v in variants]
    variants += [
        ("mixed_phase17", cfg, True, True, 1, 17),
        ("mixed_phase37", cfg, True, True, 1, 37),
    ]
    for name, c, pf, kv, pace, phase in variants:
        c = mesh.resolve(c)
        raw = workload(c, pf, kv, pace, phase)
        cohort = [r for r in raw["rows"] if r["category"] in ["initialize", "decode"]]
        identity = hashlib.sha256(json.dumps(cohort, sort_keys=True).encode()).hexdigest()
        if cohort_hash is not None and identity != cohort_hash:
            raise ValueError("comparison changed the Decode cohort")
        cohort_hash = identity
        result = mesh.run(c, raw, output / name, executable)
        point = dict(name=name, **summarize(output / name, result))
        point["p99_vs_reference_alone"] = point["latency"]["p99"] / (
            points[0]["latency"]["p99"] if points else point["latency"]["p99"]
        )
        point["cohort_sha256"] = identity
        points.append(point)
    with (output / "interference.csv").open("w") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "case",
                "count",
                "min",
                "mean",
                "p50",
                "p95",
                "p99",
                "max",
                "p99_vs_reference_alone",
                "decode_window_B_per_cycle",
                "last_decode_done",
            ]
        )
        for p in points:
            writer.writerow(
                [
                    p["name"],
                    *[
                        p["latency"][k]
                        for k in ["count", "min", "mean", "p50", "p95", "p99", "max"]
                    ],
                    p["p99_vs_reference_alone"],
                    p["decode_window_bytes_per_cycle"],
                    p["decode_last_done"],
                ]
            )
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(11, 5))
    ax.bar([p["name"] for p in points], [p["latency"]["p99"] for p in points])
    ax.set(
        ylabel="Decode release-to-done p99 (cycles)",
        title="Matched 128-request cohort; uncalibrated synthetic traffic",
    )
    ax.tick_params(axis="x", labelrotation=40)
    fig.tight_layout()
    fig.savefig(output / "interference.svg")
    plt.close(fig)
    lines = [
        "# Decode interference baseline",
        "",
        "Same 128 Decode requests in every case. Nearest-rank percentiles; finite cohort, not a steady-state or probabilistic QoS guarantee.",
        f"Decode releases: [{START}, {END}), period {PERIOD}; background releases: [3072, {END}). All completions are included in cohort latency, including drain.",
        "Initialization is identical and checked complete before background starts. Background address-slot reuse is completion-dependent; offered rate does not bypass backpressure.",
        "Prefill is synthetic SRAM writes; KV is DDR-to-SRAM copy. These are bus traffic surrogates, not full NPU inference.",
        "",
        "| Case | p50 | p95 | p99 | p99/alone | Decode B/cycle in release window |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for p in points:
        lines.append(
            f"| {p['name']} | {p['latency']['p50']} | {p['latency']['p95']} | {p['latency']['p99']} | {p['p99_vs_reference_alone']:.3f} | {p['decode_window_bytes_per_cycle']:.3f} |"
        )
    lines += [
        "",
        "![Decode tail](interference.svg)",
        "",
        "checks.json stores per-stage distributions, top router/output/VN stall counters and native SRAM counters. Stage means add; percentile values do not.",
        "Request/response transport includes network queueing. Target timing combines adapter wait and native service. Stall counters are aggregate whole-run events across resources, not additive Decode cycles.",
        "The half-rate case changes background workload explicitly; phase cases shift only background releases; resource probes keep mixed workload identical. Ratios use the original alone configuration, not a separate alone baseline for each resource variant. No PPA/RTL calibration or QoS mechanism is claimed.",
    ]
    (output / "report.md").write_text("\n".join(lines) + "\n")
    return dict(
        overall_status="PASS",
        execution_status="OK",
        sources=mesh.hashes(),
        points=points,
        checks=[
            dict(name="matched_decode_cohort", status="PASS"),
            dict(name="all_byte_oracles_and_resource_checks", status="PASS"),
            dict(name="single_fragment_trace_stage_conservation", status="PASS"),
        ],
    )


def qos_experiment(cfg, output, executable):
    """Same full background task set, different implemented scheduling policies."""
    if cfg["shape_bytes_per_cycle"] or any(
        e["priority_context"] != 64 or e["issue_limit"] for e in cfg["endpoints"]
    ):
        raise ValueError("QoS baseline must have shaping/target policy disabled")
    raw = workload(cfg, True, True)

    def target(priority=64, age=128, limit=4):
        return cfg | {
            "endpoints": [
                cfg["endpoints"][0],
                cfg["endpoints"][1]
                | {"priority_context": priority, "age_cycles": age, "issue_limit": limit},
            ]
        }

    variants = [
        ("alone", cfg, workload(cfg)),
        ("mixed", cfg, raw),
        ("fifo_limit4", target(), raw),
        ("priority_limit4", target(1), raw),
        ("fifo_limit1", target(limit=1), raw),
        ("priority_limit1", target(1, limit=1), raw),
        ("shape8", cfg | {"shape_bytes_per_cycle": 8, "shape_burst_bytes": 1024}, raw),
        ("shape16", cfg | {"shape_bytes_per_cycle": 16, "shape_burst_bytes": 1024}, raw),
        ("combined8", target(1) | {"shape_bytes_per_cycle": 8, "shape_burst_bytes": 1024}, raw),
        ("combined16", target(1) | {"shape_bytes_per_cycle": 16, "shape_burst_bytes": 1024}, raw),
        (
            "combined8_burst256",
            target(1) | {"shape_bytes_per_cycle": 8, "shape_burst_bytes": 256},
            raw,
        ),
        (
            "combined8_age32",
            target(1, 32) | {"shape_bytes_per_cycle": 8, "shape_burst_bytes": 1024},
            raw,
        ),
    ]
    points = []
    mixed_hash = None
    for name, c, trace in variants:
        result = mesh.run(mesh.resolve(c), trace, output / name, executable)
        if name != "alone":
            if mixed_hash is not None and result["input_sha256"] != mixed_hash:
                raise ValueError("QoS comparison changed full workload")
            mixed_hash = result["input_sha256"]
        p = dict(name=name, **summarize(output / name, result))
        with (output / name / "completions.tsv").open() as f:
            rows = list(csv.DictReader(f, delimiter="\t"))
        p["background"] = {
            category: dict(
                tasks=sum(c["category"] == category for c in rows),
                bytes=sum(int(c["bytes"]) for c in rows if c["category"] == category),
                window_bytes_per_cycle=sum(
                    int(c["bytes"])
                    for c in rows
                    if c["category"] == category and START <= int(c["done"]) < END
                )
                / (END - START),
                last_done=max(
                    (int(c["done"]) for c in rows if c["category"] == category), default=None
                ),
            )
            for category in ["prefill", "kv"]
        }
        p["shape_stalls"] = result["metrics"]["shape_stalls"]
        p["shaped_bytes"] = result["metrics"]["shaped_bytes"]
        p["workload_sha256"] = result["input_sha256"]
        p["p99_ratio"] = p["latency"]["p99"] / (
            points[0]["latency"]["p99"] if points else p["latency"]["p99"]
        )
        points.append(p)
    with (output / "qos.csv").open("w") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "case",
                "decode_p95",
                "decode_p99",
                "p99_ratio",
                "prefill_window_B_per_cycle",
                "kv_window_B_per_cycle",
                "all_done_cycle",
                "shape_stalls",
                "shaped_bytes",
            ]
        )
        for p in points:
            w.writerow(
                [
                    p["name"],
                    p["latency"]["p95"],
                    p["latency"]["p99"],
                    p["p99_ratio"],
                    p["background"]["prefill"]["window_bytes_per_cycle"],
                    p["background"]["kv"]["window_bytes_per_cycle"],
                    p["counters"]["cycles"],
                    p["shape_stalls"],
                    p["shaped_bytes"],
                ]
            )
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(2, 1, figsize=(10, 7), sharex=True)
    names = [p["name"] for p in points]
    axes[0].bar(names, [p["latency"]["p99"] for p in points])
    axes[0].set_ylabel("Decode p99 (cycles)")
    for cat in ["prefill", "kv"]:
        axes[1].plot(
            names,
            [p["background"][cat]["window_bytes_per_cycle"] for p in points],
            marker="o",
            label=cat,
        )
    axes[1].set_ylabel("Window completed B/cycle")
    axes[1].legend()
    axes[1].tick_params(axis="x", labelrotation=35)
    fig.tight_layout()
    fig.savefig(output / "qos.svg")
    plt.close(fig)
    return dict(
        overall_status="PASS",
        execution_status="OK",
        sources=mesh.hashes(),
        points=points,
        checks=[
            dict(name="identical_complete_mixed_workload", status="PASS"),
            dict(name="byte_oracles_resource_bounds_and_decode_stages", status="PASS"),
        ],
    )
