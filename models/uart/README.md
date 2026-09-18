# uart：基础 SystemC 外设

状态唯一见 [registry.yaml](../../registry.yaml) 的 `aixsilicon:esl:uart:0.1.0`。提供 `register` profile：探索 MMIO32 ABI、完整数据、LT 阻塞事务与参数化服务时间，不宣称厂商寄存器兼容或周期精确性能。

- `systemc/include/uart/config.hpp` 与 `model.hpp`：公开 Config/Model/端口。
- `systemc/src/model.cpp`：实现；`model.yaml`：可执行交付合同。
- [设计与寄存器](docs/design.md)、[上层集成](docs/integration.md)、[验证范围](docs/verification.md)。
- 库 target `aix::esl::uart`，安装包 `AixEslUart`；模型不含 sc_main。
- 行为/standalone/双实例/安装消费者测试共用 [peripheral_system](../../examples/peripheral_system/README.md)，不复制 fixture 到模型内。

复用现有 MMIO32/包导出实现；新增部分是本外设状态机。Python 参考不适用：独立已知字节和寄存器/时间预期由 C++ 消费者检查，无算法镜像。
