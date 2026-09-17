"""命令式 DMA（R14）。

合同（规划 §4.4 / mini_pipeline §5/§6.1）：
- 高层命令、多任务、有限队列、读写竞争、真实数据、完成/错误。
- load/store 共享单个非抢占 server；每个方向每 chunk 服务时间 4+ceil(bytes/32) ticks
  （pipeline_analytic profile 的聚合时间；不得在 MemoryPort 重复计费）。
- 完成时数据可见（写操作在该服务结束时才可见）。
- 行为核心可在 functional 与 resource profile 复用。
"""
from __future__ import annotations

from dataclasses import dataclass, field

from common.base.errors import CapacityError
from common.resources.queue import BoundedQueue
from common.resources.service import ServiceResource


@dataclass
class DmaCommand:
    op: str            # "load" | "store"
    src: int           # 源地址（load 为外存地址，store 为 slot 地址）
    dst: int           # 目标地址
    length: int        # 字节数
    task_id: str = ""
    status: str = "pending"   # pending | accepted | completed | failed

    def __hash__(self):
        return id(self)


class Dma:
    """命令式 DMA：共享 load/store 资源 + 真实字节搬移。

    pipeline_analytic 时间规则：每方向每 chunk 服务 ticks = 4 + ceil(bytes/32)。
    外部内存与 SRAM 为 ByteStore（零时间数据服务）；聚合时间已涵盖搬运假设。
    """

    def __init__(self, queue_depth: int = 4, name: str = "dma", bytes_per_32: int = 32):
        self.name = name
        self._resource = ServiceResource(
            capacity=1, queue_depth=queue_depth, name=f"{name}.server",
            service_ticks_fn=lambda cmd: 4 + (cmd.length + bytes_per_32 - 1) // bytes_per_32,
        )
        self._pending = []  # 已提交未接受（等待容量）的保留列表（不丢请求）

    def submit(self, cmd: DmaCommand) -> None:
        """提交命令；队列满则保留在 pending（容量释放后重试），不丢请求。"""
        cmd.status = "pending"
        try:
            self._resource.accept(cmd)
            cmd.status = "accepted"
        except CapacityError:
            self._pending.append(cmd)

    def step(self, clock_ticks: int = 1, ext_mem=None, sram=None) -> list:
        """推进时钟，执行真实搬移；返回本步完成命令。"""
        # 1) pending 重试入队
        still = []
        for cmd in self._pending:
            try:
                self._resource.accept(cmd)
                cmd.status = "accepted"
            except CapacityError:
                still.append(cmd)
        self._pending = still
        # 2) 资源推进
        completed = self._resource.step(clock_ticks)
        for cmd in completed:
            if cmd.op == "load":
                data = ext_mem.read(cmd.src, cmd.length)
                sram.write(cmd.dst, data)          # 完成时数据可见
            elif cmd.op == "store":
                data = sram.read(cmd.src, cmd.length)
                ext_mem.write(cmd.dst, data)
            cmd.status = "completed"
        return completed

    def drain(self, ext_mem=None, sram=None) -> list:
        completed = []
        while self._pending or len(self._resource.queue) or self._resource.busy_count():
            completed.extend(self.step(1, ext_mem, sram))
        return completed

    def busy(self) -> bool:
        return self._resource.busy_count() > 0 or len(self._resource.queue) > 0 or bool(self._pending)
