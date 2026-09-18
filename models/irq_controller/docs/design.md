# 电平中断控制器

| offset | 寄存器 | 访问/复位 | 行为 |
|---|---|---|---|
| 0x00 | PENDING | R/W1C / 0 | 对应输入的粘滞挂起位 |
| 0x04 | ENABLE | RW / 0 | 1 允许输出，0 屏蔽但仍捕获 pending |
| 0x08 | RAW | RO | 当前输入电平 |
| 0x0c | PRIORITY | RO | pending & enable 中最低编号；无源返回 32 |

Config sources 取 1..32，构造固定 sc_vector 长度。输入升高即置 pending；输入降低不会自动清除。W1C 后重新采样 RAW，仍为高电平的源立即重挂起。因此服务程序先清外设源，再清控制器 pending。不存在的输入位写 1 拒绝，RO 写拒绝。

IRQ = any(pending & enable)。源在屏蔽期间仍会挂起，解除屏蔽即可观察；不实现 claim/complete、抢占或 GIC/PLIC 兼容。reset 清 pending/enable；释放时仍为高的输入重新挂起，mask 默认仍为 0。request_drain 只关闭 MMIO，外部信号仍被采样。
