# timer

SystemC 3.0.2 / C++17 的探索 ABI 外设。状态唯一见 [registry](../../registry.yaml)，实际参数与端口见 [model.yaml](model.yaml)。无算法 oracle 需求，不创建 Python 镜像。

- [寄存器与行为](docs/design.md)
- [公开接入合同](docs/integration.md)
- [验证范围](docs/verification.md)

源码位于 systemc/include/timer 与 systemc/src；文档位于 docs。专项测试与外部消费者共用 [interrupt_system](../../examples/interrupt_system/README.md)，避免复制测试顶层。该入口同时承担 standalone/源码/安装集成测试。
