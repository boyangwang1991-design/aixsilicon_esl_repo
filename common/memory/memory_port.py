"""MemoryPort（R10）：异步读写端口。

合同（规划 §4.1）：
- 读在服务完成时取样，写在完成时可见；请求期间写 payload 保持有效。
- 存在同址并发访问时遵循资源服务顺序（单一 owner），其他 ordering 显式声明。
- 完成必须对应规定的数据可见性；成功计数不能早于写入完成。
"""
from __future__ import annotations

from ..base.errors import CapacityError, DataError
from ..transport.contracts import MemoryRequest


class MemoryPort:
    """异步内存端口：submit 请求，step 推进完成后执行访问。

    - submit(req)：入队；队列满抛 CapacityError（调用方保留并重试）。
    - step(clock_ticks)：推进；返回本步完成的内存访问结果列表
      [{"request_id", "status", "data"}]（读返回数据，写返回 None）。
    """

    def __init__(self, store, queue_depth: int = 4, name: str = "memport"):
        self.store = store
        self.name = name
        self._queue = []
        self._max = queue_depth
        self._serving = []

    def submit(self, req: MemoryRequest) -> None:
        if len(self._queue) >= self._max:
            raise CapacityError(f"{self.name}: 队列满（depth={self._max}）", field="queue_depth", value=self._max)
        self._queue.append(req)

    def step(self, clock_ticks: int = 1) -> list:
        """每个 tick 接纳一项到 serving 并立即完成（服务 1 tick）。"""
        done = []
        for _ in range(clock_ticks):
            if self._queue and len(self._serving) < 1:
                self._serving.append(self._queue.pop(0))
            if self._serving:
                req = self._serving.pop(0)
                done.append(self._execute(req))
        return done

    def _execute(self, req: MemoryRequest) -> dict:
        try:
            if req.read:
                data = self.store.read(req.address, req.length)
                return {"request_id": req.request_id, "status": "completed", "data": data}
            data = req.data if req.data is not None else b"\x00" * req.length
            self.store.write(req.address, data, req.byte_enable)
            return {"request_id": req.request_id, "status": "completed", "data": None}
        except DataError as exc:
            return {"request_id": req.request_id, "status": "failed", "error": str(exc)}

    def drain(self) -> list:
        done = []
        while self._queue or self._serving:
            done.extend(self.step(1))
        return done
