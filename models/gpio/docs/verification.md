# GPIO 验证范围

共享 fixture 为 [peripheral_system](../../../examples/peripheral_system/README.md)；行为/standalone/source/install/move 使用同一独立消费者。

| 集成项 | CTest 用例/判据 |
|---|---|
| I01/I02/I07 | peripheral_* 在 source/install/moved-prefix 构建运行；由 validate_basic_models 保存命令/hash/安装文档检查 |
| I03 | peripheral_dual：不同 width、方向与输出的两个 GPIO 独立 |
| I04 | peripheral_errors、unbound_gpio、invalid_gpio：无效位、RO/地址/长度/byte-enable、未绑定和无效 width |
| I05 | peripheral_gpio_reset：状态/IRQ/方向清除和再次触发；gpio_mmio_reset/gpio_mmio_drain/gpio_capacity：在途取消、关闭准入、已有访问完成及恢复 |
| I06 | peripheral_gpio_edges：方向/输入裁剪、masked pending/unmask、W1C 与新上升沿、级联 IRQ；gpio_collision：同刻 set 优先；transport：delay 只计费一次 |

状态 oracle 为独立常量/端口值；只有 register profile、没有可切换观测，跨 profile/观测差分不适用。GPIO 无自主排队任务，MMIO 单在途容量是其背压范围；输入引脚在 drain 时继续采样。

不证明电气/去抖/CDC、周期精度、厂商 ABI、宿主多线程安全或任意二进制兼容；未穷举所有端口缺失及配置边界。实际运行状态唯一见 [验证记录](../../../docs/basic_models_validation.md) 和 registry.evidence。
