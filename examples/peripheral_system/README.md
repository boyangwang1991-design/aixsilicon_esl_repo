# peripheral_system：B1 UART/GPIO 消费者

独立顶层通过公开包组合 host_master、tlm_bus、两个 UART、两个 GPIO 和 irq_controller。UART/GPIO 的实例参数不同，用于证明状态与配置独立；不复制模型实现。SystemC 3.0.2/C++17；仅 register/LT/full_data，探索 ABI。

| 模型实例 | 系统基址 | 窗口 | IRQ source |
|---|---|---|---|
| controller | 0x20001000 | 4 KiB | 输出到 testbench irq |
| uart0 | 0x20002000 | 4 KiB | 0 |
| gpio0 | 0x20003000 | 4 KiB | 1 |
| uart1 | 0x21002000 | 4 KiB | 2 |
| gpio1 | 0x21003000 | 4 KiB | 3 |

UART TX 接容量为 1 的外部 FIFO，RX 由 receive 或 loopback 注入；GPIO inputs/outputs/output_enable 显式绑定 sc_signal。上层 SC_THREAD 使用 host.read/write；总线将系统地址译为外设局部地址。本例 IRQ 编号是自身配置，不等于未来完整 SoC 的编号。主机无 CPU/ISS/IRQ 输入端口，testbench 观察 controller.irq。

从 ESL Repo 根执行，SYSTEMC_HOME 指向 3.0.2 安装：

```bash
cmake -S examples/peripheral_system -B build/peripheral-source -DESL_MODELS_SOURCE_DIR="$PWD"
cmake --build build/peripheral-source -j 4
ctest --test-dir build/peripheral-source --output-on-failure

cmake -S . -B build/models -DESL_ENABLE_SYSTEMC=ON -DCMAKE_INSTALL_PREFIX="$PWD/build/prefix"
cmake --build build/models -j 4
cmake --install build/models
cmake -S examples/peripheral_system -B build/peripheral-installed -DCMAKE_PREFIX_PATH="$PWD/build/prefix"
cmake --build build/peripheral-installed -j 4
ctest --test-dir build/peripheral-installed --output-on-failure

mv build/prefix build/moved-prefix
cmake -S examples/peripheral_system -B build/peripheral-moved -DCMAKE_PREFIX_PATH="$PWD/build/moved-prefix"
cmake --build build/peripheral-moved -j 4
ctest --test-dir build/peripheral-moved --output-on-failure
```

统一工具 `uv run python tools/validate_basic_models.py` 运行 RAM、B0、B1 两组消费者与安装文档检查；workflow 内使用根 uv 环境并传完整脚本路径。每次创建新的 runs 目录保存命令、日志、源 hash、JUnit 场景计数；当前证据见 [验证记录](../../docs/basic_models_validation.md)。

测试通过明确数据/时间/错误码判据，1 us 仿真 watchdog 与每用例 15 s 宿主超时；top 仅在所有工作完成且 drain/idle 后接受成功。行为及 I01–I07 映射见 [UART 验证](../../models/uart/docs/verification.md) 与 [GPIO 验证](../../models/gpio/docs/verification.md)。本例不含 timer；timer/IRQ 组合由 [interrupt_system](../interrupt_system/README.md) 验证，RAM/ROM 由 [basic_system](../basic_system/README.md) 验证。三者共享可安装模型库，不宣称已经组成完整 CPU 启动系统。
