# 可编程定时器

| offset | 寄存器 | 访问/复位 | 行为 |
|---|---|---|---|
| 0x00 | CONTROL | RW / 0 | bit0 enable、bit1 periodic、bit2 irq_enable；保留位非零拒绝 |
| 0x04 | RELOAD | RW / reset_reload | 正整数 tick 数；0 拒绝 |
| 0x08 | REMAINING | RO / 0 | 剩余 tick 向上取整；停用时 0 |
| 0x0c | PENDING | R/W1C / 0 | bit0 挂起；保留位非零拒绝 |

写 CONTROL 或运行中写 RELOAD，从该访问完成时重新计时。单次到期置 pending 并清 enable；周期模式每隔 reload*tick 置 pending。IRQ = pending && irq_enable。仅修改 irq_enable 也会重启正在运行的周期，这是本探索 ABI 的明确约定。

同一目标时间到期与 W1C 相遇时，到期 set 优先，与进程调度先后无关；下一时刻才可清除此事件。重编程唤醒计时线程，旧 deadline 不再生效。reset 清 pending/control，恢复 reset_reload 并取消旧请求；request_drain 停止未来周期但保留 pending，可在 resume 后通过 W1C 清除。模型不调用 sc_stop。

Config tick > 0、reset_reload > 0；access_latency 可为零。构造检查最大 32-bit reload 区间乘法溢出；临近 SystemC 时间上限、绝对 deadline 不可表示时给出 fatal 诊断。不是经过硬件校准的定时器。
