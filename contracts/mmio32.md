# 基础外设 MMIO32 合同 v1

用于 timer、irq_controller、uart 与 gpio 的探索 ABI，不声明厂商寄存器、GIC/PLIC、16550 或 CPU 软件兼容。

- TLM-2 LT、32-bit socket、局部字节地址；仅 4 字节对齐的 4 字节读写，小端编码。
- streaming_width >= 4；byte enable 可省略，或循环覆盖四个字节且全部 FF。部分使能返回 BYTE_ENABLE_ERROR，不静默修改未启用字节。
- 不支持的命令/RO 写/保留字段值返回 COMMAND_ERROR；未定义寄存器/非对齐地址返回 ADDRESS_ERROR；无效长度/数据指针/streaming 返回 BURST_ERROR；满或关闭准入及复位取消返回 GENERIC_ERROR。
- 单个目标最多一个在途访问，包括 incoming delay 和服务；满时立即拒绝，不排队/自动重试。调用者在 SC_THREAD/SC_CTHREAD 中阻塞调用，持有 payload 到返回。
- 成功接收后依次消费 incoming delay 和 access_latency，返回 delay=0；寄存器操作在完成时提交。协议预检查/容量拒绝不消费 delay；未知 offset/字段在服务完成时返回错误。总线自己的路由时延另计。
- reset 为必需绑定的异步高有效 sc_in<bool>；断言后取消旧 epoch 访问，清寄存器状态、IRQ，关闭准入。撤销 reset 后由顶层确认 idle 并调用 resume。断言期间 resume 失败；reset 后旧通知不能缩短新请求服务时间。
- request_drain 关闭 MMIO 准入，已接收访问有界结束；timer 同时停止未来计时，已接收的 ENABLE 写在完成时若准入已关闭则失败，避免重新启动。controller 的电平采样仍继续。
- timer idle 包含没有活动计时及 MMIO 空闲；controller idle 指 MMIO 空闲。先关上游准入、依次 drain，等待 idle_event 时用 idle() 作为谓词。host/bus 仍不提供热复位，系统复位前应先排空它们。
- IRQ 输出由单一 SC_METHOD 驱动，寄存器副作用与级联 IRQ 经 delta-cycle 传播。读取 sc_signal 前让系统到达稳定 delta；不假定 b_transport 返回时所有级联输出已更新。
- 不支持 AT、DMI、debug、窄访问、部分字节写或周期精确总线时序。DMI=false，debug 返回 0。

共用实现位于 common/systemc/include/aix/esl/mmio32.hpp，仅作为模型内部 helper，不是独立发布 ABI。完整测试见 examples/interrupt_system。

UART/GPIO 的字节/信号接口及寄存器定义见各模型 docs/design.md 和 docs/integration.md，消费者为 examples/peripheral_system。UART drain 完成依赖外部 TX FIFO 的消费者继续读取，idle 包含 TX 队列但不包含 RX 缓存；GPIO idle 仅指 MMIO 空闲，drain 时引脚采样继续。UART ERROR/GPIO PENDING 同刻 set 优先于 W1C。
