# UART 字节外设设计

本模型用于事务级软件可见功能探索，采用 [MMIO32](../../../contracts/mmio32.md)，不是 16550/PL011 或位级串口。默认 TX/RX 深度各 4（1..65535），frame_time=10 ns（正值），access_latency=1 ns（允许零）；frame_time 直接指定每帧服务时间，无隐含波特率换算。

| 局部 offset | 名称 | 语义/复位值 |
|---|---|---|
| 0x00 | DATA | 写入低 8 位字节进 TX；大于 255 拒绝；读出并弹出 RX 头；空读/满写 GENERIC_ERROR |
| 0x04 | STATUS | RO：bit0 RX 非空，bit1 TX 满，bit2 TX 空，bit3 overrun；复位为 4 |
| 0x08 | CONTROL | RW：bit0 loopback，bit1 RX 非空 IRQ，bit2 TX 空 IRQ，bit3 overrun IRQ；复位 0；其余位拒绝 |
| 0x0c | LEVELS | RO：高 16 位 TX 占用，低 16 位 RX 占用；复位 0 |
| 0x10 | ERROR | bit0 sticky RX overrun、W1C；复位 0；保留位拒绝 |

TX 深度包含排队和正在服务/等待外部接收的帧，帧顺序严格 FIFO。成功 DATA 写在 MMIO 完成时入队，队首连续服务 frame_time 后交外部 FIFO；接收端满则保持该帧，后续帧不能越过。loopback 在入队时快照，完成时直接进入内部 RX，不写外部 tx。RX 满时丢弃新到字节并置 overrun；旧字节不变。receive 返回 false 也用于 reset/drain 拒绝，这种关闭准入不产生 overrun。

同一目标时间发生 overrun 与 ERROR W1C 时 set 优先，即使相隔 delta-cycle 也保留错误；稍后时刻 W1C 才清除。IRQ 是各 enable 与当前状态的 OR，读取 RX 最后一字节解除 RX IRQ，软件关闭相应 enable 可屏蔽，清 error 不影响其他来源。

一次 reset 取消内部 TX/RX 与未完成 MMIO，epoch 阻止旧帧在重启后输出；已交外部 FIFO 数据不撤回。排空及外部服务条件见 [集成](integration.md)。无独立 Python 算法模型、DMA、AT、DMI、debug、奇偶校验/帧错/真实终端副作用或吞吐校准。
