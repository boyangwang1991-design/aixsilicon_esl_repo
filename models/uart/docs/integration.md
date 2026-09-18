# uart 集成

公开类型 `aix::esl::uart::Model`、`Config`，构造为 `Model(sc_module_name, Config={})`；参数在 elaboration 前确定，默认与范围见 [manifest](../model.yaml)。

源码树：`add_subdirectory(${ESL_MODELS_SOURCE_DIR}/models/uart models/uart)`；安装消费：`find_package(AixEslUart 0.1 CONFIG REQUIRED)`。两者均用 `target_link_libraries(consumer PRIVATE aix::esl::uart)`，包含 `<uart/model.hpp>`；C++17/SystemC 3.0.2，由包导出解析 SystemC 依赖。

`registers` 为必需绑定的 32-bit multi target socket，可绑定多个主机但仅一个在途 MMIO；局部字节地址由上层总线转换。`reset` 是必需高有效 sc_in<bool>，`irq` 为必需高有效 sc_out<bool>。完整事务约定见 [MMIO32](../../../contracts/mmio32.md)。

`tx` 是必需 sc_fifo_out<unsigned char>，绑定单一外部字节接收 FIFO，loopback 模式仍须绑定。`receive(unsigned char)` 是外部字节注入方法，仅从仿真内核线程/方法调用，返回是否接收；不可从宿主 OS 线程直接调用。无 rx 端口与位级波形。输出阻塞由外部 FIFO 的容量和消费者读速率决定。

request_drain 关闭新 MMIO 和 receive，已接收的 MMIO/TX 继续完成；idle 指 MMIO 空闲且 TX 队列为空，RX 缓存不算在途。排空需要外部持续读取 TX FIFO，不能承诺无消费者时有界结束。loopback 在 drain 中仍可把已接收 TX 转成 RX。reset 清内部 TX/RX、control/error 并取消旧 MMIO/帧；已经交给外部 FIFO 的字节由环境持有，reset 不撤回。resume 仅在 reset 撤销、idle 后成功。

等待 idle_event 时先检查 idle()；IRQ/信号经 delta-cycle 传播后观察。顶层持有 sc_main、停止策略、时间精度。系统热复位前先 drain host/bus；本模型可取消并不意味着整条互连支持热复位。

可直接复现的 source/install/move 命令与地址映射见 [独立消费者](../../../examples/peripheral_system/README.md)。依赖公共源码只用于构建，安装消费者只包含公开头文件和包。
