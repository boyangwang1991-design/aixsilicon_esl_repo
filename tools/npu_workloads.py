"""Deterministic NPU execution plans; SystemC, not Python, owns simulated time.

Rows describe operations and dependencies, never candidate-specific completion times.
All tensor regions are disjoint. Reuse is expressed by omission of repeated loads
and explicit buffer lifetime dependencies. Units: byte, cycle, MAC.
"""

from __future__ import annotations

import json
import math
import random
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class Task:
    id: int
    port: int
    op: str
    address: int = 0
    bytes: int = 0
    beat: int = 128
    burst: int = 16
    axi_id: int = 0
    earliest: int = 0
    cycles: int = 0
    mask_period: int = 1
    wdelay: int = 0
    deps: list[int] = field(default_factory=list)


class Plan:
    def __init__(self, name, seed=0):
        self.name, self.seed = name, seed
        self.tasks: list[Task] = []
        self.tensors = []
        self.next_address = 0
        self.metadata = {
            "kind": "closed_loop_dag",
            "seed": seed,
            "profile": name,
            "local_buffer_bytes_per_tile": 65536,
            "compute_model": "serial PE waves: ceil(M/16)*ceil(N/16)*(K+30)",
        }

    def tensor(self, name, shape, element=2, ld=None):
        rows, cols = math.prod(shape[:-1]), shape[-1]
        stride = (ld or cols) * element
        stride = (stride + 7) // 8 * 8
        base = (self.next_address + 4095) // 4096 * 4096
        size = rows * stride
        self.tensors.append(
            dict(
                name=name,
                shape=list(shape),
                element_bytes=element,
                base=base,
                bytes=size,
                row_stride_bytes=stride,
            )
        )
        self.next_address = base + size
        if self.next_address > 8388608:
            raise ValueError("tensor allocation exceeds shared SRAM")
        return base, stride

    def task(
        self,
        port,
        op,
        address=0,
        size=0,
        deps=(),
        cycles=0,
        beat=None,
        burst=16,
        mask_period=1,
        wdelay=0,
        earliest=0,
    ):
        if beat is None:
            beat = next((b for b in (128, 64, 32, 16, 8) if size % b == 0 and address % b == 0), 8)
        t = Task(
            len(self.tasks),
            port,
            op,
            address,
            size,
            beat,
            burst,
            len(self.tasks) % 16,
            earliest,
            cycles,
            mask_period,
            wdelay,
            sorted(set(deps)),
        )
        self.tasks.append(t)
        return t.id

    def rows(self, port, op, base, stride, row, column, height, width_bytes, deps=()):
        return [
            self.task(port, op, base + (row + i) * stride + column, width_bytes, deps)
            for i in range(height)
        ]

    def check(self):
        read = written = 0
        for t in self.tasks:
            if any(d >= t.id or d < 0 for d in t.deps):
                raise ValueError("DAG requires earlier dependencies")
            if not 0 <= t.port < 8 or not 0 <= t.axi_id < 16:
                raise ValueError("port/ID")
            if t.op == "C":
                if t.cycles <= 0:
                    raise ValueError("compute cycles")
                continue
            if t.bytes <= 0 or t.bytes % t.beat or t.address % t.beat:
                raise ValueError("aligned transfer")
            if not any(
                x["base"] <= t.address and t.address + t.bytes <= x["base"] + x["bytes"]
                for x in self.tensors
            ):
                raise ValueError("task outside declared tensor")
            if t.op == "R":
                read += t.bytes
            else:
                written += t.bytes // t.beat * math.ceil(t.beat / t.mask_period)
        self.metadata.update(
            tasks=len(self.tasks),
            read_bytes=read,
            write_bytes=written,
            allocated_shared_bytes=self.next_address,
            tensors=self.tensors,
            compute_cycles=sum(t.cycles for t in self.tasks if t.op == "C"),
        )
        return self.metadata

    def save(self, directory):
        directory = Path(directory)
        directory.mkdir(parents=True, exist_ok=True)
        self.check()
        path = directory / (self.name + ".dag")
        rows = []
        for t in self.tasks:
            dep = ",".join(map(str, t.deps)) or "-"
            rows.append(
                f"{t.id} {t.port} {t.op} {t.address} {t.bytes} {t.beat} {t.burst} "
                f"{t.axi_id} {t.earliest} {t.cycles} {t.mask_period} {t.wdelay} {dep}"
            )
        path.write_text("\n".join(rows) + "\n")
        path.with_suffix(".json").write_text(json.dumps(self.metadata, indent=2) + "\n")
        return path


def gemm(name="gemm", seed=0, holdout=False, low_parallel=False, concurrent=False):
    p = Plan(name, seed)
    m, n, k = (192, 192, 128) if holdout else (256, 128, 128)
    if low_parallel:
        m, n, k = (16, 128, 192 if holdout else 256)
    # Power-of-two leading dimension is deliberate NPU tensor alignment, fixed
    # across candidates. The independent padded-layout experiment changes it.
    leading = 520 if holdout else 512
    a, sa = p.tensor("A", (m, k), ld=leading)
    b, sb = p.tensor("B", (k, n), ld=leading)
    out, so = p.tensor("C", (m, n), element=4, ld=n)
    tile = 64
    local_bytes = 2 * (tile * tile * 2 + tile * tile * 2) + tile * tile * 4
    assert local_bytes <= 65536
    tail = [[] for _ in range(8)]
    tiles = 4 if concurrent else (2 if low_parallel else 8)
    initial = []
    if concurrent:
        # Explicitly produce inputs before computation can consume them.
        initial = [
            p.task(4, "W", a, m * sa, beat=64, wdelay=100),
            p.task(5, "W", b, k * sb, beat=64, wdelay=100),
        ]
    for ti, (mi, ni) in enumerate((mi, ni) for mi in range(0, m, tile) for ni in range(0, n, tile)):
        port = ti % tiles
        h, w = min(tile, m - mi), min(tile, n - ni)
        previous = None
        compute = []
        for ki in range(0, k, tile):
            kk = min(tile, k - ki)
            deps = tail[port] + initial + ([compute[-2]] if len(compute) >= 2 else [])
            loads = p.rows(port, "R", a, sa, mi, ki * 2, h, kk * 2, deps)
            loads += p.rows(port, "R", b, sb, ki, ni * 2, kk, w * 2, deps)
            cyc = math.ceil(h / 16) * math.ceil(w / 16) * (kk + 30)
            previous = p.task(
                port, "C", deps=loads + ([] if previous is None else [previous]), cycles=cyc
            )
            compute.append(previous)
        stores = p.rows(port, "W", out, so, mi, ni * 4, h, w * 4, [previous])
        if concurrent:
            drain_port = 6 + ti % 2
            stores = p.rows(drain_port, "R", out, so, mi, ni * 4, h, w * 4, stores)
        tail[port] = stores
    p.metadata.update(
        shape=[m, n, k],
        tile=[64, 64, 64],
        leading_dimension=leading,
        loop="output_tile_then_K",
        local_footprint_bytes=local_bytes,
        reuse="C stationary across K; ping-pong A/B; no inter-output-tile B cache",
        external_source="64 B/cycle per load DMA, first W +100 cycles"
        if concurrent
        else "preinitialized",
    )
    return p


def attention(name="prefill", seed=0, holdout=False, decode=False):
    p = Plan(name, seed)
    seq, heads, dim = (96, 4, 64) if holdout else (64, 2, 64)
    if decode:
        seq, heads, dim = (384, 4, 128) if holdout else (256, 4, 64)
    qlen = 1 if decode else seq
    q, sq = p.tensor("Q", (heads, qlen, dim))
    k, sk = p.tensor("K", (heads, seq + 1, dim))
    v, sv = p.tensor("V", (heads, seq + 1, dim))
    o, so = p.tensor("O", (heads, qlen, dim))
    for head in range(heads):
        port = head % 8
        previous_output = []
        for qi in range(0, qlen, 16):
            qn = min(16, qlen - qi)
            qload = p.rows(port, "R", q, sq, head * qlen + qi, 0, qn, dim * 2, previous_output)
            previous = None
            computes = []
            for ki in range(0, seq, 32):
                count = min(32, seq - ki)
                deps = previous_output + ([computes[-2]] if len(computes) >= 2 else [])
                loads = p.rows(port, "R", k, sk, head * (seq + 1) + ki, 0, count, dim * 2, deps)
                loads += p.rows(port, "R", v, sv, head * (seq + 1) + ki, 0, count, dim * 2, deps)
                deps = loads + qload + ([] if previous is None else [previous])
                qk = p.task(
                    port,
                    "C",
                    deps=deps,
                    cycles=math.ceil(qn / 16) * math.ceil(count / 16) * (dim + 30),
                )
                soft = p.task(
                    port,
                    "C",
                    deps=[qk],
                    cycles=math.ceil(qn * count / 16) * 3 + math.ceil(qn * count / 4) + 16,
                )
                previous = p.task(
                    port,
                    "C",
                    deps=[soft],
                    cycles=math.ceil(qn / 16) * math.ceil(dim / 16) * (count + 30)
                    + math.ceil(qn * dim / 16),
                )
                computes.append(previous)
            previous_output = p.rows(port, "W", o, so, head * qlen + qi, 0, qn, dim * 2, [previous])
        if decode:
            # Append a new KV token after all reads of the old visible length.
            # 16-byte producer chunks model small writes without invented full beats.
            for base, stride in ((k, sk), (v, sv)):
                for off in range(0, dim * 2, 16):
                    p.task(
                        port,
                        "W",
                        base + (head * (seq + 1) + seq) * stride + off,
                        16,
                        previous_output,
                        beat=16,
                    )
    footprint = 16 * dim * 2 + 2 * 2 * 32 * dim * 2 + 16 * 32 * 4 + 16 * dim * 4 + 16 * 8
    assert footprint <= 65536
    p.metadata.update(
        sequence=seq,
        heads=heads,
        head_dim=dim,
        local_footprint_bytes=footprint,
        attention="online tiled, no materialized full score matrix",
        layout="head_token_dim",
        vector_model="16 elements/cycle reduction/scale; 4 exp/cycle; serial stages",
    )
    return p


def convolution(name="convolution", seed=0, holdout=False):
    p = Plan(name, seed)
    h = 24 if holdout else 16
    cin = cout = 32
    x, sx = p.tensor("input", (h, h, cin))
    w, sw = p.tensor("weights", (3, 3, cin, cout))
    y, sy = p.tensor("output", (h, h, cout), element=4)
    tails = [[] for _ in range(8)]
    for ti, (row, col) in enumerate((r, c) for r in range(0, h, 8) for c in range(0, h, 8)):
        port = ti % 8
        loads = []
        for r in range(max(0, row - 1), min(h, row + 9)):
            left, right = max(0, col - 1), min(h, col + 9)
            loads.append(
                p.task(port, "R", x + (r * h + left) * sx, (right - left) * cin * 2, tails[port])
            )
        loads.append(p.task(port, "R", w, 3 * 3 * cin * cout * 2, tails[port]))
        compute = p.task(
            port, "C", deps=loads, cycles=math.ceil(8 * 8 * cout * 3 * 3 * cin / 256) + 30
        )
        tails[port] = [
            p.task(port, "W", y + ((row + r) * h + col) * sy, 8 * cout * 4, [compute])
            for r in range(8)
        ]
    p.metadata.update(
        shape=[1, h, h, cin, cout],
        kernel=[3, 3],
        algorithm="direct NHWC, halo loads once per tile; zero padding omitted",
        local_footprint_bytes=2 * 10 * 10 * cin * 2 + 3 * 3 * cin * cout * 2 + 8 * 8 * cout * 4,
    )
    assert p.metadata["local_footprint_bytes"] <= 65536
    return p


def embedding(name="embedding", seed=0, holdout=False):
    p = Plan(name, seed)
    rng = random.Random(seed)
    count = 96 if holdout else 64
    dim = 128
    table, stride = p.tensor("table", (4096, dim))
    idx, si = p.tensor("indices", (count, 4), element=2)
    out, so = p.tensor("gather_output", (count, dim))
    last = [[] for _ in range(16)]
    indices = []
    for i in range(count):
        port = i % 8
        slot = i % 16
        index = rng.randrange(16 if rng.random() < (0.7 if holdout else 0.5) else 4096)
        indices.append(index)
        index_read = p.task(port, "R", idx + i * si, 8, last[slot], beat=8)
        data = p.task(port, "R", table + index * stride, dim * 2, [index_read])
        store = p.task(port, "W", out + i * so, dim * 2, [data])
        last[slot] = [store]
    p.metadata.update(
        indices=indices,
        index_semantics="trace materializes index values; dependency waits for index read",
        distribution="mixture uniform table / uniform hot set; no dedup",
        local_footprint_bytes=2 * dim * 2,
    )
    return p


def transpose(name="transpose", seed=0, holdout=False):
    p = Plan(name, seed)
    h, w = (96, 128) if holdout else (64, 128)
    src, ss = p.tensor("source", (h, w))
    dst, sd = p.tensor("destination", (w, h))
    tails = [[] for _ in range(8)]
    for i, (r, c) in enumerate((r, c) for r in range(0, h, 32) for c in range(0, w, 32)):
        port = i % 8
        loads = p.rows(port, "R", src, ss, r, c * 2, 32, 64, tails[port])
        compute = p.task(port, "C", deps=loads, cycles=64)
        tails[port] = p.rows(port, "W", dst, sd, c, r * 2, 32, 64, [compute])
    p.metadata.update(tile=[32, 32], local_footprint_bytes=4096)
    return p


def micro(name="micro", pattern="stride", seed=0, count=16):
    p = Plan(name, seed)
    base, _ = p.tensor("memory", (4194304,), element=1)
    rng = random.Random(seed)
    for port in range(8):
        for i in range(count):
            if pattern == "stride":
                address = port * 4096 + i * 1024
            elif pattern == "xor_adversary":
                address = port * 32768 + i * 32768
            elif pattern == "stream":
                address = port * 128 + i * 1024
            elif pattern == "random":
                address = rng.randrange(8192) * 128
            else:
                raise ValueError(pattern)
            p.task(port, "R", base + address, 128, burst=1)
    return p


def suite(directory, holdout=False, seeds=(11, 23, 37, 53, 71)):
    plans = [
        gemm("gemm", holdout=holdout),
        gemm("gemm_tail", holdout=holdout, low_parallel=True),
        attention("prefill", holdout=holdout),
        attention("decode", holdout=holdout, decode=True),
        convolution(holdout=holdout),
        transpose(holdout=holdout),
        gemm("concurrent", holdout=holdout, concurrent=True),
    ]
    plans += [embedding("embedding_" + str(seed), seed, holdout) for seed in seeds]
    return [p.save(directory) for p in plans]
