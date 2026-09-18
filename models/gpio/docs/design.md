# GPIO 数字行为设计

采用 [MMIO32](../../../contracts/mmio32.md) 的探索 ABI；width 默认 32（1..32），access_latency 默认 1 ns（允许零）。高于 width 的输入被屏蔽，写入任何未实现位返回 COMMAND_ERROR。不是电气/三态/去抖/CDC 或厂商兼容模型。

| 局部 offset | 名称 | 语义/复位值 |
|---|---|---|
| 0x00 | DIRECTION | RW，1 输出、0 输入；复位 0 |
| 0x04 | OUTPUT | RW，输出锁存值；实际 outputs=OUTPUT & DIRECTION；复位 0 |
| 0x08 | INPUT | RO，当前 inputs & 有效位 & ~DIRECTION |
| 0x0c | IRQ_ENABLE | RW，pending 到 IRQ 的掩码；复位 0 |
| 0x10 | PENDING | 上升沿锁存、W1C；复位 0 |

只对输入方向位采样上升沿，下降沿不触发；pending 与掩码独立，屏蔽期间仍锁存，之后 unmask 可触发 IRQ。清 pending 后输入持续高不重触发，必须先下降再上升。方向写在完成时更新并以当前引脚重建采样基线，方向切换本身不产生上升沿。输出锁存值在改变方向时保留。

硬件上升沿与软件 W1C 在同一目标时间发生时 set 优先（包括不同 delta-cycle），下一时刻可清除。IRQ=bool(PENDING & IRQ_ENABLE)，输出信号与 IRQ 由单一 SC_METHOD 驱动。reset 清方向、输出、enable、pending 并取消未完成 MMIO，以当前输入重建基线，撤销后显式 resume。drain 期间仍采样输入，idle 仅指 MMIO 空闲，详见 [集成](integration.md)。
