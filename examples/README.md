# SystemC 系统示例

本目录只保存多个模型的显式装配、工作负载和系统 scoreboard。单模型接入放 models/<name>/examples/integration；公共实现放 common/systemc。

- [basic_system](basic_system/README.md)：已运行的 B0 双主机/总线/RAM/ROM，支持源码与安装消费。
- [interrupt_system](interrupt_system/README.md)：B1 timer/IRQ 子集，含寄存器、IRQ/reset 与标准集成验证。
- B1 UART/GPIO 与 B2 DMA 仍按 [任务账本](../docs/esl_todo.md) 推进，不创建空示例目录。

环境冒烟移至 [tests/environment](../tests/README.md)，历史 Python mini_pipeline 移至 [reference](../reference/README.md)。
