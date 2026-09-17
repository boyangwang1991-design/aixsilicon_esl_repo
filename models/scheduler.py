"""简单 Scheduler（R17）：mini_pipeline 的确定性调度。

合同（mini_pipeline §5/§6.1）：
- 提交 load→compute→store DAG；不模拟 CPU 指令；不把每个元素变成独立任务。
- Scheduler 只判断依赖与 slot 分配；接受后由资源端仲裁
  （DMA store 优先、同类 chunk ID 升序；Compute 最小 ID）。
- 容量满时未接受项保留在 Scheduler（或资源 pending），容量释放后重试，不丢请求。
- 每个 tick 先批量处理完成，再更新依赖和 slot 分配，随后接纳命令，最后仲裁。
"""
from __future__ import annotations

from common.base.event import TaskGraph
from common.base.errors import CapacityError
from models.compute import ComputeShell
from models.dma import Dma, DmaCommand
from models.bmu import Bmu
from common.resources.service import ServiceResource


class Chunk:
    """一个 chunk 的状态：load/compute/store 三任务 + slot。"""

    def __init__(self, chunk_id: int):
        self.chunk_id = chunk_id
        self.slot = None            # BMU handle
        self.load_done = False
        self.compute_done = False
        self.store_done = False
        self.failed = False


class Scheduler:
    """自包含调度器：持有 DMA/Compute/BMU/内存并逐 tick 推进。"""

    def __init__(self, dma: Dma, compute: ComputeShell, bmu: Bmu,
                 ext_mem, sram, num_chunks: int,
                 compute_ticks_fn=None, store_priority: bool = True,
                 chunk_elements: int = 256, chunk_bytes: int = 512):
        self.dma = dma
        self.compute = compute
        self.bmu = bmu
        self.ext_mem = ext_mem
        self.sram = sram
        self.store_priority = store_priority
        self.chunk_elements = chunk_elements
        self.chunk_bytes = chunk_bytes
        self.chunks = [Chunk(i) for i in range(num_chunks)]
        # Compute 单 server 非抢占：每 chunk 4 + ceil(elements/8) ticks
        self._compute_res = ServiceResource(
            capacity=1, queue_depth=4, name="compute.server",
            service_ticks_fn=compute_ticks_fn or
            (lambda c: 4 + (self.chunk_elements + 7) // 8),
        )
        self._graph = TaskGraph()
        for c in self.chunks:
            cid = str(c.chunk_id)
            self._graph.add_dependency(f"{cid}:compute", f"{cid}:load")
            self._graph.add_dependency(f"{cid}:store", f"{cid}:compute")
        self._compute_inflight = set()     # 已在 compute 队列/服务中的 chunk（防重复提交）
        self._dma_inflight = set()         # 已提交到 DMA 的 task_id（防重复提交）

    # ---- 命令构造 ----
    def _load_cmd(self, c: Chunk) -> DmaCommand:
        # 外存输入区 -> slot 输入区（slot 地址：input_offset = slot*1024）
        in_addr = c.slot.slot_id * 1024
        return DmaCommand(op="load", src=self.chunk_elements * 2 * c.chunk_id,
                          dst=in_addr, length=self.chunk_bytes,
                          task_id=f"{c.chunk_id}:load")

    def _store_cmd(self, c: Chunk) -> DmaCommand:
        out_slot_addr = c.slot.slot_id * 1024 + self.chunk_bytes
        out_mem_addr = 0x10000 + self.chunk_elements * 2 * c.chunk_id
        return DmaCommand(op="store", src=out_slot_addr, dst=out_mem_addr,
                          length=self.chunk_bytes,
                          task_id=f"{c.chunk_id}:store")

    # ---- 逐 tick 推进 ----
    def step(self, clock_ticks: int = 1) -> None:
        for _ in range(clock_ticks):
            self._tick()

    def _tick(self) -> None:
        # 1) 推进 DMA（完成数据搬移）
        for cmd in self.dma.step(1, self.ext_mem, self.sram):
            self._dma_inflight.discard(cmd.task_id)
            c = self.chunks[int(cmd.task_id.split(":")[0])]
            if c.failed:
                continue
            if cmd.op == "load" and not c.load_done:
                c.load_done = True
                self._graph.mark_done(f"{c.chunk_id}:load")
            elif cmd.op == "store" and not c.store_done:
                c.store_done = True
                self._graph.mark_done(f"{c.chunk_id}:store")
                if c.slot is not None:
                    self.bmu.free(c.slot)
                    c.slot = None
        # 2) 推进 Compute：完成时执行真实计算并写回 slot 输出区
        for c in self._compute_res.step(1):
            self._compute_inflight.discard(c.chunk_id)
            if not c.failed and c.slot is not None:
                in_addr = c.slot.slot_id * 1024
                data = self.sram.read(in_addr, self.chunk_bytes)
                out = self.compute.compute(data, self.chunk_elements)
                out_addr = in_addr + self.chunk_bytes
                self.sram.write(out_addr, out)   # 完成时输出对 DMA 可见
                c.compute_done = True
                self._graph.mark_done(f"{c.chunk_id}:compute")
        # 3) 更新依赖与 slot 分配（空闲 slot 按 chunk ID 升序）
        for c in sorted(self.chunks, key=lambda x: x.chunk_id):
            if c.slot is None and not c.store_done and not c.failed:
                try:
                    c.slot = self.bmu.alloc()
                except CapacityError:
                    break  # 池满，等下一 tick
        # 4) 接纳命令（store 优先，同类 chunk ID 升序）
        for op, c in self._ready_commands():
            if op == "load" and not c.load_done and c.slot is not None:
                tid = f"{c.chunk_id}:load"
                if tid not in self._dma_inflight:
                    self.dma.submit(self._load_cmd(c))
                    self._dma_inflight.add(tid)
            elif op == "store" and not c.store_done:
                tid = f"{c.chunk_id}:store"
                if tid not in self._dma_inflight:
                    self.dma.submit(self._store_cmd(c))
                    self._dma_inflight.add(tid)
            elif op == "compute" and not c.compute_done:
                self._try_enqueue_compute(c)

    def _ready_commands(self):
        """生成当前就绪命令列表，按 store 优先 / chunk ID 升序。"""
        items = []
        for c in sorted(self.chunks, key=lambda x: x.chunk_id):
            if c.failed:
                continue
            if not c.load_done and c.slot is not None:
                items.append(("load", c))
            elif c.load_done and not c.compute_done:
                items.append(("compute", c))
            elif c.compute_done and not c.store_done:
                items.append(("store", c))
        if self.store_priority:
            items.sort(key=lambda x: (0 if x[0] == "store" else 1, x[1].chunk_id))
        return items

    def _try_enqueue_compute(self, c: Chunk) -> None:
        if c.chunk_id in self._compute_inflight:
            return  # 已提交，防重复入队
        try:
            self._compute_res.accept(c)
            self._compute_inflight.add(c.chunk_id)
        except CapacityError:
            pass  # 队列满，下一 tick 重试（不丢）

    # ---- 状态 ----
    def done(self) -> bool:
        return all(c.store_done or c.failed for c in self.chunks)

    def any_failed(self) -> bool:
        return any(c.failed for c in self.chunks)

    def drain(self) -> int:
        """推进直到 done；返回最后完成事件发生的 tick 序号（从 0 起）。"""
        calls = 0
        while not self.done():
            self._tick()
            calls += 1
            if calls > 10_000_000:
                raise RuntimeError("drain 超限（疑似死锁）")
        # 第 n 次 _tick 处理 tick=n-1；完成发生在最后一次 _tick 内
        return calls - 1
