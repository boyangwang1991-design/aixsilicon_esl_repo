"""ByteStore（R10）：真实字节存储。

合同（规划 §4.1/§4.2）：
- 每个物理地址只有一个权威数据存储（唯一 owner）。
- 地址边界检查、byte enable（非空时与请求长度一致）、完成时数据可见。
- 基础 copy 拒绝源/目标重叠、越界/整数溢出/零长度。
"""
from __future__ import annotations

from ..base.errors import ConfigError, DataError


class ByteStore:
    """定长字节存储。

    - read(addr, length) / write(addr, data, byte_enable=None) 为同步接口
      （MemoryPort 负责异步服务语义；本类只管真实字节）。
    - 越界抛 DataError；byte_enable 非空时长度须等于数据长度。
    """

    def __init__(self, size_bytes: int, base: int = 0, name: str = "mem"):
        if size_bytes <= 0:
            raise ConfigError(f"{name}: size_bytes 必须 > 0", field="size_bytes", value=size_bytes)
        self.name = name
        self.base = base
        self._size = size_bytes
        self._data = bytearray(size_bytes)

    @property
    def size(self) -> int:
        return self._size

    def _check_range(self, addr: int, length: int) -> None:
        if length <= 0:
            raise DataError(f"{self.name}: 零长度访问", position=addr)
        if addr < self.base or addr + length > self.base + self._size:
            raise DataError(f"{self.name}: 越界访问 addr={addr} len={length} "
                            f"(范围 [{self.base},{self.base + self._size}))",
                            position=addr)

    def read(self, addr: int, length: int) -> bytes:
        self._check_range(addr, length)
        off = addr - self.base
        return bytes(self._data[off:off + length])

    def write(self, addr: int, data: bytes, byte_enable=None) -> None:
        length = len(data)
        self._check_range(addr, length)
        if byte_enable is not None:
            if len(byte_enable) != length:
                raise DataError(f"{self.name}: byte_enable 长度 {len(byte_enable)} != 数据长度 {length}",
                                position=addr)
        off = addr - self.base
        for i, b in enumerate(data):
            if byte_enable is None or byte_enable[i]:
                self._data[off + i] = b

    def fill(self, value: int = 0) -> None:
        """整片清零/填充（reset 用）。"""
        self._data = bytearray([value & 0xFF]) * self._size

    def snapshot(self) -> bytes:
        return bytes(self._data)
