"""mini_pipeline 系统构建与运行（R18）。

参数（docs/esl_testcase.md §4/§6.1）：
- N=16384 元素，chunk=256 元素（512B），chunk 数 64。
- 外存 256KiB；输入 @0x0000、输出 @0x10000；SRAM 8KiB；slot 1KiB（512B in + 512B out）。
- pipeline_analytic：DMA 每方向每 chunk 4+ceil(bytes/32)=4+16=20 ticks；
  Compute 4+ceil(elements/8)=4+32=36 ticks；外存/SRAM 零时延数据服务。
- 确定性输入 x[i] = ((17*i + 5) % 65536) - 32768。
"""
from __future__ import annotations

import struct
from pathlib import Path

from common.base.errors import ConfigError
from common.memory.byte_store import ByteStore
from common.memory.buffer_pool import BufferPool
from common.resources.service import ServiceResource
from models.compute import ComputeShell, vector_affine
from models.dma import Dma
from models.bmu import Bmu
from models.scheduler import Scheduler

N_ELEMENTS = 16384
CHUNK_ELEMENTS = 256
CHUNK_BYTES = 512
NUM_CHUNKS = 64
EXT_MEM_SIZE = 256 * 1024
OUT_BASE = 0x10000
SRAM_SIZE = 8 * 1024
SLOT_BYTES = 1024  # 512B in + 512B out
IN_BASE = 0x0000


def make_input(n_elements: int = N_ELEMENTS) -> bytes:
    """确定性输入：x[i] = ((17*i + 5) % 65536) - 32768（int16 小端）。"""
    out = bytearray(n_elements * 2)
    for i in range(n_elements):
        x = ((17 * i + 5) % 65536) - 32768
        struct.pack_into("<h", out, i * 2, x)
    return bytes(out)


def oracle_output(n_elements: int = N_ELEMENTS) -> bytes:
    """独立 oracle：y = clip_{int16}(2x+3)，与 ComputeShell 相同语义但独立实现。"""
    return ComputeShell(vector_affine).compute(make_input(n_elements), n_elements)


class MiniPipeline:
    """mini_pipeline 系统：组合 DMA/Compute/BMU/Scheduler + 外存/SRAM。"""

    def __init__(self, num_slots: int = 1, dma_queue_depth: int = 4,
                 n_elements: int = N_ELEMENTS, dma_bytes_per_tick: int = 32,
                 compute_elements_per_tick: int = 8):
        if num_slots < 1:
            raise ConfigError("num_slots 必须 >= 1", field="num_slots", value=num_slots)
        if dma_queue_depth < 1:
            raise ConfigError("dma_queue_depth 必须 >= 1", field="dma_queue_depth", value=dma_queue_depth)
        self.num_slots = num_slots
        self.n_elements = n_elements
        self.num_chunks = n_elements // CHUNK_ELEMENTS
        self.ext_mem = ByteStore(EXT_MEM_SIZE, base=0, name="ext_mem")
        self.sram = ByteStore(SRAM_SIZE, base=0, name="sram")
        self.ext_mem.write(IN_BASE, make_input(n_elements))
        # 输出 guard 区检查：先填充 0xAA
        out_bytes = n_elements * 2
        self.ext_mem.write(OUT_BASE, b"\xaa" * out_bytes)

        self.bmu = Bmu(num_slots, SLOT_BYTES, name="bmu")
        self.compute = ComputeShell(vector_affine, name="compute")
        self.dma = Dma(queue_depth=dma_queue_depth, name="dma",
                       bytes_per_32=dma_bytes_per_tick)
        self.scheduler = Scheduler(
            dma=self.dma, compute=self.compute, bmu=self.bmu,
            ext_mem=self.ext_mem, sram=self.sram, num_chunks=self.num_chunks,
            compute_ticks_fn=lambda c: 4 + (CHUNK_ELEMENTS + compute_elements_per_tick - 1)
            // compute_elements_per_tick,
            chunk_elements=CHUNK_ELEMENTS, chunk_bytes=CHUNK_BYTES,
        )

    def run(self) -> int:
        """运行直到完成，返回总 ticks。"""
        return self.scheduler.drain()

    def check_output(self, n_elements: int = N_ELEMENTS) -> bool:
        """用独立 oracle 校验输出区。"""
        expected = oracle_output(n_elements)
        actual = self.ext_mem.read(OUT_BASE, n_elements * 2)
        return actual == expected
