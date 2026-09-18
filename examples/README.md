# SystemC 系统示例

本目录只保存多个模型的显式装配、工作负载和系统 scoreboard。单模型接入可放 models/<name>/examples/integration，或在模型文档引用已有共享消费者；公共实现放 common/systemc。

- [basic_system](basic_system/README.md)：已运行的 B0 双主机/总线/RAM/ROM，支持源码与安装消费。
- [interrupt_system](interrupt_system/README.md)：B1 timer/IRQ 子集，含寄存器、IRQ/reset 与标准集成验证。
- [peripheral_system](peripheral_system/README.md)：B1 UART/GPIO 子集，含双实例、字节/信号接口、IRQ、背压/reset/drain。
- [dma_system](dma_system/README.md)：B2 命令式 DMA、双实例、共享 RAM/主机竞争、错误/reset/drain。

环境冒烟移至 [tests/environment](../tests/README.md)，历史 Python mini_pipeline 移至 [reference](../reference/README.md)。
