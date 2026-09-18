# B1 定时器/中断闭环

SystemC 顶层显式装配 host_master → tlm_bus → timer/irq_controller；两个 timer 验证实例独立性，主机线程观察控制器输出信号，无 CPU/ISS/OS。

| 地址 | 目标 |
|---|---|
| 0x20000000 / 4 KiB | timer0 |
| 0x20001000 / 4 KiB | irq_controller |
| 0x21000000 / 4 KiB | timer1（仅测试用窗口） |

控制器输入：0=timer0，1=测试激励，2=timer1，3=未使用/false。reset 高有效，必需端口显式绑定。仿真上限 1 us，CTest 超时 15 s；结束时所有模型 drain/idle。UART/GPIO 仍 planned，这里仅完成 B1 timer/IRQ 子集。

从资产仓库根执行，SYSTEMC_HOME 指向已安装的 3.0.2：

```bash
cmake -S examples/interrupt_system -B build/interrupt-source -DESL_MODELS_SOURCE_DIR="$PWD"
cmake --build build/interrupt-source -j 4
ctest --test-dir build/interrupt-source --output-on-failure
# 根 CMake 安装到 prefix 后独立消费
cmake -S examples/interrupt_system -B build/interrupt-installed -DCMAKE_PREFIX_PATH=/path/to/prefix
cmake --build build/interrupt-installed -j 4
ctest --test-dir build/interrupt-installed --output-on-failure
```

完整 producer/install/relocation 回归复用 tools/validate_basic_models.py，同时回归 B0。端口/访问协议见 [MMIO32](../../contracts/mmio32.md)，寄存器见各模型 docs/design.md。
