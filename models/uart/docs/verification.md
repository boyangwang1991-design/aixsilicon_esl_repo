# UART 验证范围

共享 fixture 为 [peripheral_system](../../../examples/peripheral_system/README.md)；同一源码同时承担行为、standalone 与外部消费测试，无重复 tests 目录。

| 集成项 | CTest 用例/判据 |
|---|---|
| I01/I02/I07 | 全部 peripheral_* 用例在 source/install/moved-prefix 执行；公开 target、包、环境/hash/日志由 validate_basic_models 记录 |
| I03 | peripheral_dual：两个 UART 的独立字节、配置与 FIFO |
| I04 | peripheral_errors、unbound_uart、invalid_uart：非法事务、未绑定端口、零容量；rx_overrun：满/空 |
| I05 | peripheral_backpressure：满 TX 拒绝、不丢/不重复、外部 FIFO 阻塞后恢复、drain/resume；reset_tx：阻塞帧取消及新帧恢复；reset_mmio/drain_mmio/capacity：在途取消、排空、准入上限 |
| I06 | peripheral_uart_tx：严格 frame_time；transport：incoming delay 只消费一次；loopback：功能与级联 IRQ；uart_collision：同刻 overrun 与 W1C set 优先 |

独立已知字节和解析时间为 oracle，无 DUT 自我比较。rx_overrun 验证旧字节保留、新字节丢弃；loopback 测 IRQ 清除。只有一个 profile，无 trace 开关，跨 profile/观测差分不适用。

未证明位级波形、厂商 ABI、任意编译器 ABI 互换、宿主多线程安全、公平性或硬件性能；未逐一穷举所有未绑定端口/全部参数边界。当前运行结果唯一见 [验证记录](../../../docs/basic_models_validation.md) 与 registry.evidence，不复制测试 PASS 清单。
