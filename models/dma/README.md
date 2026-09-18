# dma：SystemC 命令式搬运引擎

规范 ID 为 `aixsilicon:esl:dma:0.1.0`，旧五段 ID 仅作为发现别名，状态唯一见 [registry.yaml](../../registry.yaml)。SystemC/C++17 库 target `aix::esl::dma`，安装包 `AixEslDma`；支持 command/LT/full_data profile。

实现正长度、非重叠的分块 copy，有限命令/完成容量、共享读写发起端、错误与已写字节数、reset/drain。不是整命令原子写，不提供 MMIO、IRQ、SG、stride、厂商 descriptor 或硬件吞吐校准。

- `systemc/include/dma/config.hpp`：Config、Command、枚举。
- `systemc/include/dma/model.hpp`：Model、Completion、公开 TLM socket/方法。
- `systemc/src/model.cpp`：唯一实现；`model.yaml`：交付合同。
- [设计](docs/design.md)、[集成](docs/integration.md)、[验证](docs/verification.md)。

复用标准 TLM、已有 RAM/总线和公共 CMake 打包。现有 register_target 模板不提供命令队列，因此单独实现该状态机；测试共享 [dma_system](../../examples/dma_system/README.md)，不在模型内复制 fixture。

不新增 Python 模型。独立 C++ 已知字节/内存与时间预期用于验证。历史 reference/legacy_python 的 DMA 保留为原型，不因 ID 别名自动成为此模型的实现或验收证据。任务状态见 [唯一账本](../../docs/esl_todo.md)。
