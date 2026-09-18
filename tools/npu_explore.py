"""Repo-owned SystemC SRAM experiment runner and offline evidence/report generation."""

from __future__ import annotations

import csv
import hashlib
import html
import json
import math
import os
import shutil
import statistics
import subprocess
import time
from pathlib import Path

import npu_workloads as workloads
import yaml

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "models/npu_sram_controller"
DEFAULTS = dict(
    ports=8,
    banks=32,
    word_bytes=32,
    stripe_bytes=32,
    groups=4,
    capacity=8388608,
    cycle_limit=2000000,
    mapping="modulo",
    topology="flat",
    queue="voq",
    arbitration="rr",
    xor_shift=0,
    group_first=0,
    local_xor=0,
    dual_port=0,
    dual_ingress=0,
    full_data=0,
    read_latency=2,
    write_latency=1,
    bank_ii=1,
    outstanding=32,
    ids=16,
    ingress_entries=128,
    bank_entries=8,
    rob_beats=128,
    w_beats=128,
    completion_entries=8,
    rmw_contexts=4,
    lanes=4,
    return_bytes=128,
    remote_bytes=128,
    link_entries=16,
    link_buffer_bytes=512,
    link_latency=1,
    credit_delay=1,
    matching_rounds=4,
    age_guard=256,
    max_read_grants=16,
    ecc_bytes=0,
    ecc_lanes=4,
    ecc_ii=1,
    ecc_latency=1,
    ecc_group=0,
    macro_word_write=0,
    scrub_interval=0,
    correction_latency=1,
    trace_limit=20000,
)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_hashes():
    paths = list(MODEL.rglob("*")) + [
        Path(__file__),
        ROOT / "tools/npu_workloads.py",
        ROOT / "tools/esl_cli.py",
    ]
    paths += list((ROOT / "cmake").glob("*")) + [
        ROOT / "common/systemc/include/aix/esl/byte_store.hpp",
        ROOT / "contracts/environment.json",
    ]
    return {
        str(p.relative_to(ROOT)): digest(p)
        for p in sorted(paths)
        if p.is_file() and "__pycache__" not in p.parts and "reports" not in p.relative_to(ROOT).parts
    }


def command(argv, log, timeout=180):
    with Path(log).open("w") as out:
        result = subprocess.run(
            list(map(str, argv)), stdout=out, stderr=subprocess.STDOUT, timeout=timeout
        )
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}); log: {log}")


def build(output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    build_dir = output / "build"
    prefix = os.environ.get("SYSTEMC_HOME", "/home/eda/.local/systemc-3.0.2")
    command(
        [
            "cmake",
            "-S",
            MODEL,
            "-B",
            build_dir,
            "-DESL_NPU_BUILD_TESTS=ON",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DSYSTEMC_HOME={prefix}",
        ],
        output / "configure.log",
    )
    command(["cmake", "--build", build_dir, "-j4"], output / "build.log")
    return build_dir / "npu_sram_run"


def resolved(overrides):
    if not isinstance(overrides, dict):
        raise ValueError("architecture must be a mapping")
    if set(overrides) - set(DEFAULTS) - {"regions"}:
        raise ValueError(f"unknown architecture keys: {set(overrides) - set(DEFAULTS)}")
    c = {**DEFAULTS, **overrides}
    for k, v in c.items():
        if k == "regions":
            continue
        if type(DEFAULTS[k]) is int and type(v) is not int:
            raise ValueError(f"{k} must be integer")
        if type(DEFAULTS[k]) is str and not isinstance(v, str):
            raise ValueError(f"{k} must be string")
    return c


def write_config(c, path):
    rows = []
    for k, v in sorted(c.items()):
        if k == "regions":
            for r in v:
                required = {
                    "base",
                    "length",
                    "local_base",
                    "policy",
                    "stripe",
                    "shift",
                    "rotation",
                    "banks",
                }
                if set(r) != required:
                    raise ValueError("region fields")
                rows.append(
                    "region="
                    + " ".join(
                        str(r[x])
                        for x in (
                            "base",
                            "length",
                            "local_base",
                            "policy",
                            "stripe",
                            "shift",
                            "rotation",
                        )
                    )
                    + " "
                    + ",".join(map(str, r["banks"]))
                )
        else:
            rows.append(f"{k}={v}")
    Path(path).write_text("\n".join(rows) + "\n")


def resource_proxy(c):
    # Explicit bookkeeping proxy, not area. Header, data and response networks.
    b, w, p = c["banks"], c["word_bytes"], c["ports"]
    metadata = 32 * 8 * (p * c["ingress_entries"] + b * c["bank_entries"])
    buffers = metadata + 8 * (
        p * (c["rob_beats"] + c["w_beats"]) * 128
        + b * c["completion_entries"] * w
        + b * c["rmw_contexts"] * (w + 32)
    )
    flat = (b * (p * c["lanes"] - 1) * (w + 32) + p * c["lanes"] * (b - 1) * (w + 8)) * 8
    if c["topology"] == "hierarchical":
        groups = c["groups"]
        local_b = b // groups
        local_p = p // groups
        switches = (
            groups
            * (
                local_b * (local_p * c["lanes"] + 1) * (w + 32)
                + local_p * c["lanes"] * local_b * (w + 8)
            )
            * 8
        )
        switches += groups * (groups - 1) * c["remote_bytes"] * 8 * 2
    else:
        switches = flat
    # Each modeled link has queues; count the instantiated graph conservatively.
    links = 4 * (p + b) if c["topology"] == "flat" else 4 * (p + b + 3 * c["groups"])
    buffers += links * (c["link_buffer_bytes"] + c["link_entries"] * 32) * 8
    return dict(
        buffer_bits=buffers,
        switch_proxy_bits=switches,
        ecc_lanes=2 * b * c["ecc_lanes"] if c["ecc_bytes"] else 0,
        note="specified capacity/mux proxy; excludes synthesized macro and wire effects",
    )


class Experiment:
    def __init__(self, output, binary, hashes):
        self.output = Path(output)
        self.binary = Path(binary)
        self.hashes = hashes
        self.points = []

    def run(self, name, c, workload, phase="train", trace=False):
        c = resolved(c)
        workload = Path(workload)
        token = hashlib.sha256(
            (json.dumps(c, sort_keys=True) + digest(workload) + phase).encode()
        ).hexdigest()[:16]
        directory = self.output / "runs" / f"{phase}-{name}-{workload.stem}-{token}"
        directory.mkdir(parents=True, exist_ok=True)
        cfg = directory / "config.kv"
        write_config(c, cfg)
        result_path = directory / "metrics.json"
        manifest = dict(
            architecture=name,
            config=c,
            config_sha256=digest(cfg),
            workload=str(workload),
            workload_sha256=digest(workload),
            phase=phase,
            window="full workload including startup/drain",
            source_sha256=self.hashes,
            binary_sha256=digest(self.binary),
            resource_proxy=resource_proxy(c),
            data_mode="full_data" if c["full_data"] else "traffic_only",
            calibration="NOT_RUN",
        )
        (directory / "run.json").write_text(json.dumps(manifest, indent=2) + "\n")
        argv = [self.binary, cfg, workload, result_path]
        if trace:
            argv.append(directory / "trace.jsonl")
        start = time.monotonic()
        try:
            command(argv, directory / "stderr.log", timeout=90)
            result = json.loads(result_path.read_text())
            if result["status"] != "PASS":
                raise ValueError("DUT run failed")
            if (
                result["accepted"] != result["completed"]
                or result["fragments"] != result["fragments_done"]
            ):
                raise ValueError("conservation failed")
            service_limit = (
                math.ceil(result["cycles"] / c["bank_ii"])
                * c["banks"]
                * (2 if c["dual_port"] else 1)
            )
            if result["bank_services"] > service_limit:
                raise ValueError("Bank service upper bound violated")
            result["checks"].append("bank_service_upper_bound")
        except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
            result = dict(status="FAIL", error=str(exc))
            (directory / "failure.json").write_text(json.dumps(result, indent=2) + "\n")
        # Full sample vectors remain in metrics.json; do not duplicate them in
        # every aggregate table (or use a truncated vector for quantiles).
        result.pop("latencies", None)
        point = dict(
            architecture=name,
            workload=workload.stem,
            phase=phase,
            config=c,
            run_dir=str(directory.relative_to(self.output)),
            wall_seconds=time.monotonic() - start,
            **resource_proxy(c),
            **result,
        )
        self.points.append(point)
        with (self.output / "points.jsonl").open("a") as stream:
            stream.write(json.dumps(point) + "\n")
        if len(self.points) % 25 == 0:
            (self.output / "points.json").write_text(json.dumps(self.points, indent=2) + "\n")
        return point


def summarize(points, phase, baseline="C0"):
    selected = [x for x in points if x["phase"] == phase]
    base = {x["workload"]: x for x in selected if x["architecture"] == baseline}
    names = sorted({x["architecture"] for x in selected})
    rows = []
    for name in names:
        group = [x for x in selected if x["architecture"] == name]
        if (
            len(group) != len(base)
            or any(x["status"] != "PASS" for x in group)
            or any(x["status"] != "PASS" for x in base.values())
        ):
            rows.append(
                dict(architecture=name, status="FAIL", reason="missing/failed matched workload")
            )
            continue
        families = {}
        ratios = {}
        for x in group:
            if x["workload"] not in base:
                raise ValueError("unmatched workload")
            r = x["makespan"] / base[x["workload"]]["makespan"]
            ratios[x["workload"]] = r
            family = "embedding" if x["workload"].startswith("embedding_") else x["workload"]
            families.setdefault(family, []).append(r)
        means = {f: math.exp(statistics.mean(map(math.log, v))) for f, v in families.items()}
        score = math.exp(statistics.mean(map(math.log, means.values())))
        random_ratios = families.get("embedding", [])
        if len(random_ratios) > 1:
            logs = list(map(math.log, random_ratios))
            radius = 2.776 * statistics.stdev(logs) / math.sqrt(len(logs))
            ci = [
                math.exp(statistics.mean(logs) - radius),
                math.exp(statistics.mean(logs) + radius),
            ]
        else:
            ci = None
        rows.append(
            dict(
                architecture=name,
                status="PASS",
                score=score,
                worst=max(ratios.values()),
                families=means,
                ratios=ratios,
                embedding_ratio_95ci=ci,
                buffer_bits=group[0]["buffer_bits"],
                switch_proxy_bits=group[0]["switch_proxy_bits"],
                goal_10pct=score <= 0.9,
                goal_worst_10pct=max(ratios.values()) <= 1.1,
            )
        )
    return sorted(rows, key=lambda x: x.get("score", float("inf")))


def figures(output, points, ranking):
    os.environ.setdefault("MPLCONFIGDIR", str(Path(output) / "matplotlib-cache"))
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    output = Path(output)
    figdir = output / "figures"
    figdir.mkdir(exist_ok=True)
    plt.rcParams.update({"font.size": 9, "axes.spines.top": False, "axes.spines.right": False})

    def save(fig, name):
        fig.tight_layout()
        fig.savefig(figdir / (name + ".svg"))
        fig.savefig(figdir / (name + ".png"), dpi=150)
        plt.close(fig)

    rows = [r for r in ranking if r["status"] == "PASS"]
    if rows:
        rows = rows[:18]
        families = list(rows[0]["families"])
        fig, ax = plt.subplots(figsize=(11, max(4, len(rows) * 0.3)))
        values = np.array([[r["families"][f] for f in families] for r in rows])
        im = ax.imshow(
            values,
            aspect="auto",
            cmap="RdYlGn_r",
            vmin=min(0.5, float(values.min())),
            vmax=max(1.2, float(values.max())),
        )
        ax.set_xticks(range(len(families)), families, rotation=25, ha="right")
        ax.set_yticks(range(len(rows)), [r["architecture"] for r in rows])
        ax.set_title("Closed-loop makespan / C0 (lower is better)")
        fig.colorbar(im, ax=ax)
        save(fig, "npu_makespan")
        fig, ax = plt.subplots(figsize=(9, 5))
        for r in rows:
            ax.scatter(r["switch_proxy_bits"] / 1000, r["score"], s=30 + r["buffer_bits"] / 100000)
            ax.annotate(r["architecture"], (r["switch_proxy_bits"] / 1000, r["score"]), fontsize=7)
        ax.axhline(1, color="gray", linestyle="--")
        ax.set(
            xlabel="Switch proxy (kbit-mux; not silicon area)",
            ylabel="Geometric mean makespan / C0",
            title="Resource/performance candidates",
        )
        save(fig, "pareto")
    probes = [x for x in points if x["phase"] == "mapping" and x["status"] == "PASS"]
    if probes:
        for banks in (8, 16, 32, 64):
            subset = [x for x in probes if x["config"]["banks"] == banks]
            keys = sorted({(x["config"]["mapping"], x["config"]["xor_shift"]) for x in subset})
            stripes = sorted({x["config"]["stripe_bytes"] for x in subset})
            values = np.zeros((len(keys), len(stripes)))
            for i, key in enumerate(keys):
                for j, stripe in enumerate(stripes):
                    runs = [
                        x
                        for x in subset
                        if (x["config"]["mapping"], x["config"]["xor_shift"]) == key
                        and x["config"]["stripe_bytes"] == stripe
                    ]
                    values[i, j] = statistics.mean(x["makespan"] for x in runs)
            fig, ax = plt.subplots(figsize=(8, 3.5))
            im = ax.imshow(values, aspect="auto", cmap="viridis_r")
            ax.set_xticks(range(len(stripes)), stripes)
            ax.set_yticks(range(len(keys)), [f"{m} shift={s}" for m, s in keys])
            ax.set(
                xlabel="Stripe bytes",
                title=f"Mapping probes: {banks} banks (mean cycles, lower better)",
            )
            fig.colorbar(im, ax=ax)
            save(fig, f"mapping_{banks}")
    traces = [
        x
        for x in points
        if (output / x["run_dir"] / "trace.jsonl").exists() and x["status"] == "PASS"
    ]
    for point in traces:
        label = point["phase"] + "_" + point["architecture"]
        events = [
            json.loads(line)
            for line in (output / point["run_dir"] / "trace.jsonl").read_text().splitlines()
        ]
        bank = [e for e in events if e["event"] in ("bank_read", "bank_write")]
        if not bank:
            continue
        last = max(e["cycle"] for e in bank)
        nb = point["config"]["banks"]
        bins = 80
        heat = np.zeros((nb, bins))
        for e in bank:
            heat[e["bank"], min(bins - 1, e["cycle"] * bins // (last + 1))] += 1
        fig, ax = plt.subplots(figsize=(11, 4))
        im = ax.imshow(heat, aspect="auto", origin="lower", extent=(0, last, 0, nb), cmap="magma")
        ax.set(
            xlabel="Cycle",
            ylabel="Bank",
            title=point["architecture"] + " bank service timeline (bounded trace window)",
        )
        fig.colorbar(im, ax=ax)
        save(fig, "banks_" + label)
        transactions = sorted({e["transaction"] for e in events if "transaction" in e})[:24]
        fig, ax = plt.subplots(figsize=(11, 5))
        colors = {
            "dispatch": "#268bd2",
            "bank_read": "#859900",
            "bank_write": "#b58900",
            "write_commit": "#cb4b16",
            "fragment_return": "#6c71c4",
        }
        for event, color in colors.items():
            series = [
                e for e in events if e["event"] == event and e.get("transaction") in transactions
            ]
            ax.scatter(
                [e["cycle"] for e in series],
                [transactions.index(e["transaction"]) for e in series],
                s=9,
                label=event,
                color=color,
            )
        ax.set(
            xlabel="Cycle",
            ylabel="Transaction (first 24)",
            title=point["architecture"] + " fragment timeline",
        )
        ax.legend(ncol=3)
        save(fig, "timeline_" + label)
        addresses = [e for e in events if e["event"] == "dispatch" and "address" in e]
        if addresses:
            fig, ax = plt.subplots(figsize=(11, 4))
            ax.scatter(
                [e["address"] for e in addresses],
                [e["bank"] for e in addresses],
                c=[e["port"] for e in addresses],
                cmap="tab10",
                s=7,
                alpha=0.65,
            )
            ax.set(
                xlabel="Logical byte address",
                ylabel="Physical Bank",
                title=label + " actual dispatched tensor addresses (trace window)",
            )
            save(fig, "addresses_" + label)
        finished = [e for e in events if e["event"] == "task_finish"]
        if finished:
            fig, ax = plt.subplots(figsize=(11, 4))
            for op, color, offset in (
                ("R", "#268bd2", -0.25),
                ("C", "#859900", 0),
                ("W", "#cb4b16", 0.25),
            ):
                samples = [e for e in finished if e["op"] == op]
                for e in samples:
                    ax.plot(
                        [e["start"], e["cycle"]],
                        [e["port"] + offset] * 2,
                        color=color,
                        linewidth=2,
                        alpha=0.35,
                    )
                ax.plot(
                    [],
                    [],
                    color=color,
                    linewidth=3,
                    label={"R": "load", "C": "compute", "W": "store"}[op],
                )
            ax.set(
                xlabel="Cycle",
                ylabel="NPU port / Tile",
                title=label + " closed-loop task intervals",
            )
            ax.legend(ncol=3)
            save(fig, "npu_timeline_" + label)
    fig, ax = plt.subplots(figsize=(9, 4))
    for point in traces:
        metrics = json.loads((output / point["run_dir"] / "metrics.json").read_text())
        samples = sorted(metrics.get("latencies", []))
        if samples:
            ax.plot(
                samples,
                np.arange(1, len(samples) + 1) / len(samples),
                label=point["phase"] + ":" + point["architecture"],
            )
    ax.set(
        xlabel="Accepted to final R/B cycles",
        ylabel="CDF",
        title="Transaction latency (complete NPU kernels, not steady state)",
    )
    if traces:
        ax.legend(fontsize=7)
    save(fig, "latency_cdf")


def report(exp, ranking, holdout, selection):
    out = exp.output
    (out / "points.json").write_text(json.dumps(exp.points, indent=2) + "\n")
    (out / "ranking.json").write_text(
        json.dumps(dict(training=ranking, holdout=holdout, selection=selection), indent=2) + "\n"
    )
    keys = [
        "architecture",
        "workload",
        "phase",
        "status",
        "makespan",
        "payload_bytes_per_cycle",
        "p99",
        "bank_utilization",
        "rmw",
        "hol",
        "buffer_bits",
        "switch_proxy_bits",
        "run_dir",
    ]
    with (out / "results.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=keys, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(exp.points)
    figures(out, exp.points, ranking)
    valid = [r for r in holdout if r["status"] == "PASS"]
    candidate = next((r for r in valid if r["architecture"] == selection), None)
    status = "PASS" if all(p["status"] == "PASS" for p in exp.points) else "FAIL"
    summary = {
        "status": status,
        "selected_on_training": selection,
        "selection_holdout": candidate,
        "runs": len(exp.points),
        "failed_runs": sum(x["status"] != "PASS" for x in exp.points),
        "calibration": "NOT_RUN",
        "source_sha256": exp.hashes,
    }
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    lines = [
        "# NPU SRAM architecture exploration",
        "",
        f"Execution: {status}. Runs: {len(exp.points)}.",
        f"Training-selected candidate: **{selection}**. Selection frozen before holdout.",
        "",
        "Results are resource-model predictions, not measured silicon or RTL calibration.",
        "All NPU runs measure complete closed-loop DAGs, including startup; no steady-state throughput claim.",
        "Traffic-only results have separate SystemC full-data correctness tests. p99 of short runs is descriptive only.",
        "",
        "| Candidate | Training makespan/C0 | Worst workload/C0 |",
        "|---|---:|---:|",
    ]
    lines += [
        f"| {r['architecture']} | {r.get('score', float('nan')):.4f} | {r.get('worst', float('nan')):.4f} |"
        for r in ranking
    ]
    if candidate:
        lines += [
            "",
            f"Holdout selected ratio: **{candidate['score']:.4f}**, worst **{candidate['worst']:.4f}**.",
            f"10% improvement goal: {candidate['goal_10pct']}; worst regression <=10%: {candidate['goal_worst_10pct']}.",
        ]
    lines += [
        "",
        "Scope/limitations: see model docs/verification.md and docs/design.md.",
        "The finite search establishes the best evaluated candidate, not a global optimum.",
        "Resources are explicit bookkeeping proxies; frequency is fixed at nominal 1 GHz.",
        "Random embedding workloads use five paired seeds; their 95% intervals are in ranking.json.",
        "Raw configurations, workload hashes, metrics, failures and source hashes are archived per run.",
    ]
    (out / "report.md").write_text("\n".join(lines) + "\n")
    images = "".join(
        f'<figure><img src="figures/{p.name}"><figcaption>{html.escape(p.stem)}</figcaption></figure>'
        for p in sorted((out / "figures").glob("*.svg"))
    )
    table = "".join(
        "<tr>" + "".join(f"<td>{html.escape(str(r.get(k, '')))}</td>" for k in keys) + "</tr>"
        for r in exp.points
    )
    page = """<!doctype html><meta charset="utf-8"><title>NPU SRAM exploration</title>
<style>body{font:15px system-ui;margin:30px;background:#f4f6fa;color:#17283b}figure{background:white;padding:15px;border-radius:10px}img{max-width:100%}table{border-collapse:collapse;background:white;font-size:12px}td,th{padding:7px;border-bottom:1px solid #ddd}input{padding:10px;width:360px}pre{white-space:pre-wrap}</style>"""
    page += (
        f'<h1>NPU SRAM architecture exploration</h1><pre>{html.escape(chr(10).join(lines[:9]))}</pre><p><a href="report.md">Report</a> · <a href="ranking.json">Ranking</a> · <a href="results.csv">CSV</a> · <a href="points.json">All points</a></p>'
        + images
    )
    page += (
        '<h2>Search all evaluated runs</h2><input id="filter" placeholder="architecture, workload, phase or status"><table><thead><tr>'
        + "".join(f"<th>{k}</th>" for k in keys)
        + "</tr></thead><tbody>"
        + table
        + "</tbody></table>"
    )
    page += """<script>document.querySelector('#filter').oninput=e=>{let q=e.target.value.toLowerCase();document.querySelectorAll('tbody tr').forEach(r=>r.hidden=!r.textContent.toLowerCase().includes(q));};</script>"""
    (out / "index.html").write_text(page)
    return summary


def explore(output, quick=False):
    output = Path(output).resolve()
    if (output / "points.jsonl").exists() or (output / "points.json").exists():
        raise ValueError("exploration output already contains evidence; choose a fresh directory")
    output.mkdir(parents=True, exist_ok=True)
    binary = build(output)
    hashes = source_hashes()
    exp = Experiment(output, binary, hashes)
    command(
        ["ctest", "--test-dir", binary.parent, "--output-on-failure", "-j4"], output / "ctest.log"
    )
    probe_dir = output / "workloads/probes"
    probes = [
        workloads.micro(pattern=p, name=p).save(probe_dir)
        for p in ("stream", "stride", "xor_adversary")
    ]
    candidates = {"C0": resolved({})}
    mapping_scores = []
    orgs = (32,) if quick else (8, 16, 32, 64)
    stripes = (16, 32, 128) if quick else (16, 32, 64, 128, 256, 512, 2048)
    shifts = (0, 1) if quick else (0, 1, 2, 3)
    for banks in orgs:
        for stripe in stripes:
            for policy, shift in [("modulo", 0)] + [("xor", s) for s in shifts]:
                c = resolved(
                    dict(
                        banks=banks,
                        word_bytes=1024 // banks,
                        stripe_bytes=stripe,
                        lanes=128 // (1024 // banks),
                        bank_entries=256 // banks,
                        mapping=policy,
                        xor_shift=shift,
                    )
                )
                name = f"B{banks}_G{stripe}_{policy}{shift}"
                points = [exp.run(name, c, w, "mapping") for w in probes]
                if all(p["status"] == "PASS" for p in points):
                    mapping_scores.append((statistics.mean(p["makespan"] for p in points), name, c))
        print(f"mapping organization {banks} complete: {len(exp.points)} runs", flush=True)
    for banks in orgs:
        rows = sorted([x for x in mapping_scores if x[2]["banks"] == banks])
        for _, name, c in rows[:2]:
            candidates[name] = c
        # Preserve fine-grain and equal-word representatives even when the
        # synthetic probes prefer coarse stripes. They can win on NPU layouts.
        for _, name, c in rows:
            if c["stripe_bytes"] == c["word_bytes"] and (
                c["mapping"] == "modulo" or c["xor_shift"] in (0, 1)
            ):
                candidates[name] = c
    # Architecture interactions, plus causality counterexamples and ECC families.
    best = sorted(mapping_scores)[0]
    _, best_name, best_cfg = best
    for width in (64, 128, 256):
        candidates[f"H{width}_" + best_name] = {
            **best_cfg,
            "topology": "hierarchical",
            "remote_bytes": width,
        }
    candidates["FIFO_" + best_name] = {**best_cfg, "queue": "fifo"}
    candidates["O8_" + best_name] = {**best_cfg, "outstanding": 8}
    candidates["Gfirst_" + best_name] = {**best_cfg, "topology": "hierarchical", "group_first": 1}
    candidates["C_ECC"] = {**DEFAULTS, "ecc_bytes": 8}
    candidates["ECC_" + best_name] = {
        **best_cfg,
        "ecc_bytes": 8,
        "ecc_lanes": best_cfg["word_bytes"] // 8,
    }
    # Check constrained return paths without calling them a better architecture.
    candidates["R32"] = {**DEFAULTS, "return_bytes": 32}
    if not quick:
        # Complete a bounded mapping x topology x queue interaction matrix for
        # the top two organizations, rather than composing single-factor winners.
        org_best = [
            min((x for x in mapping_scores if x[2]["banks"] == b), key=lambda x: x[0]) for b in orgs
        ]
        for _, _, base_config in sorted(org_best)[:2]:
            for policy in ("modulo", "xor"):
                for topology in ("flat", "hierarchical"):
                    for queue in ("fifo", "voq"):
                        c = {**base_config, "mapping": policy, "topology": topology, "queue": queue}
                        candidates[f"I_B{c['banks']}_{policy}_{topology}_{queue}"] = c
        candidates["LOCAL_XOR_H"] = {
            **DEFAULTS,
            "mapping": "xor",
            "local_xor": 1,
            "topology": "hierarchical",
        }
        candidates["REGION"] = {
            **DEFAULTS,
            "mapping": "region",
            "regions": [
                dict(
                    base=0,
                    length=262144,
                    local_base=0,
                    policy="xor",
                    stripe=32,
                    shift=0,
                    rotation=0,
                    banks=list(range(32)),
                ),
                dict(
                    base=262144,
                    length=262144,
                    local_base=8192,
                    policy="modulo",
                    stripe=128,
                    shift=0,
                    rotation=0,
                    banks=list(range(32)),
                ),
                dict(
                    base=524288,
                    length=7864320,
                    local_base=16384,
                    policy="xor",
                    stripe=64,
                    shift=1,
                    rotation=0,
                    banks=list(range(32)),
                ),
            ],
        }
        candidates["DUAL"] = {**best_cfg, "dual_port": 1, "dual_ingress": 1}
        candidates["ECC_QUARTER"] = {**DEFAULTS, "ecc_bytes": 8, "ecc_lanes": 1}
    if quick:
        candidates = {
            k: v
            for k, v in candidates.items()
            if k in ("C0", best_name, "FIFO_" + best_name, "C_ECC", "ECC_" + best_name, "R32")
        }
    training = workloads.suite(output / "workloads/train")
    (output / "candidates.yaml").write_text(yaml.safe_dump(candidates, sort_keys=False))
    for i, (name, c) in enumerate(candidates.items()):
        for w in training:
            exp.run(name, c, w, "train", trace=(name in ("C0", best_name) and w.stem == "gemm"))
        print(f"NPU training {i + 1}/{len(candidates)}: {name}", flush=True)
    ranking = summarize(exp.points, "train")
    # Never select ECC/no-ECC across different reliability levels.
    feasible = [
        r
        for r in ranking
        if r["status"] == "PASS"
        and not candidates[r["architecture"]]["ecc_bytes"]
        and not candidates[r["architecture"]]["dual_port"]
        and r["worst"] <= 1.1
        and r["buffer_bits"] <= resource_proxy(DEFAULTS)["buffer_bits"] * 1.25
    ]
    selected = feasible[0]["architecture"] if feasible else "C0"
    frozen = {
        "selected": selected,
        "criterion": "training geometric mean; worst<=1.1; buffer<=1.25; ECC none family",
        "candidates": [r["architecture"] for r in feasible[:3]],
        "source_sha256": hashes,
    }
    (output / "selection_frozen.json").write_text(json.dumps(frozen, indent=2) + "\n")
    (output / "recommended.yaml").write_text(yaml.safe_dump(candidates[selected], sort_keys=False))
    holdout_workloads = workloads.suite(
        output / "workloads/holdout", holdout=True, seeds=(101, 127, 149, 173, 199)
    )
    finalists = list(dict.fromkeys(["C0", selected] + frozen["candidates"]))
    for name in finalists:
        for w in holdout_workloads:
            exp.run(
                name,
                candidates[name],
                w,
                "holdout",
                trace=(name in ("C0", selected) and w.stem == "gemm"),
            )
        print("holdout complete: " + name, flush=True)
    holdout = summarize(exp.points, "holdout")
    peak = workloads.micro("balanced_long", "stream", count=2048).save(
        output / "workloads/validation"
    )
    peak_result = exp.run("C0", DEFAULTS, peak, "validation")
    peak_goal = peak_result["status"] == "PASS" and peak_result["payload_bytes_per_cycle"] >= 921.6
    (output / "peak_goal.json").write_text(
        json.dumps(
            {
                "status": "PASS" if peak_goal else "FAIL",
                "metric": "full-window effective read B/cycle",
                "target": 921.6,
                "actual": peak_result.get("payload_bytes_per_cycle"),
                "run_dir": peak_result["run_dir"],
            },
            indent=2,
        )
        + "\n"
    )
    # Matched reliability comparison is separate from global C0 table.
    ecc_rank = summarize(
        [x for x in exp.points if x["architecture"] in ("C_ECC", "ECC_" + best_name)],
        "train",
        "C_ECC",
    )
    (output / "ecc_ranking.json").write_text(json.dumps(ecc_rank, indent=2) + "\n")
    return report(exp, ranking, holdout, selected)


def validate(output):
    output = Path(output).resolve()
    binary = build(output)
    command(
        ["ctest", "--test-dir", binary.parent, "--output-on-failure", "-j4"], output / "ctest.log"
    )
    fixture = MODEL / "examples/integration"
    prefix = output / "install"
    relocated = output / "relocated"
    command(["cmake", "--install", binary.parent, "--prefix", prefix], output / "install.log")
    if relocated.exists():
        raise ValueError("relocated fixture already exists; choose a fresh verification output")
    shutil.copytree(prefix, relocated)
    for kind in ("source", "install", "relocated"):
        builddir = output / ("consumer-" + kind)
        args = ["cmake", "-S", fixture, "-B", builddir]
        if kind == "source":
            args += ["-DESL_SOURCE=" + str(ROOT)]
        else:
            args += ["-DCMAKE_PREFIX_PATH=" + str(relocated if kind == "relocated" else prefix)]
        command(args, output / (kind + "-configure.log"))
        command(["cmake", "--build", builddir, "-j4"], output / (kind + "-build.log"))
        command(
            ["ctest", "--test-dir", builddir, "--output-on-failure"], output / (kind + "-ctest.log")
        )
    checks = dict(
        status="PASS",
        source_sha256=source_hashes(),
        unit_cases=14,
        consumers=["source", "install", "relocated"],
        calibration="NOT_RUN",
    )
    (output / "checks.json").write_text(json.dumps(checks, indent=2) + "\n")
    return checks


def main(args):
    if args.action == "validate":
        result = validate(args.output)
    elif args.action == "explore":
        result = explore(args.output, args.quick)
    else:
        binary = build(args.output)
        config = yaml.safe_load(Path(args.config).read_text()) if args.config else {}
        path = (
            workloads.gemm().save(Path(args.output) / "workloads")
            if not args.workload
            else args.workload
        )
        exp = Experiment(Path(args.output), binary, source_hashes())
        result = exp.run("single", config, path, trace=True)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["status"] == "PASS" else 1
