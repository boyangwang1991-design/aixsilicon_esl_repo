# gpio 集成

公开类型 `aix::esl::gpio::Model`、`Config`，构造为 `Model(sc_module_name, Config={})`；参数在 elaboration 前确定，默认与范围见 [manifest](../model.yaml)。

源码树：`add_subdirectory(${ESL_MODELS_SOURCE_DIR}/models/gpio models/gpio)`；安装消费：`find_package(AixEslGpio 0.1 CONFIG REQUIRED)`。两者均用 `target_link_libraries(consumer PRIVATE aix::esl::gpio)`，包含 `<gpio/model.hpp>`；C++17/SystemC 3.0.2，由包导出解析 SystemC 依赖。

`registers` 为必需绑定的 32-bit multi target socket，可绑定多个主机但仅一个在途 MMIO；局部字节地址由上层总线转换。`reset` 是必需高有效 sc_in<bool>，`irq` 为必需高有效 sc_out<bool>。完整事务约定见 [MMIO32](../../../contracts/mmio32.md)。

`inputs` 为必需 sc_in<sc_uint<32>>；`outputs` 和 `output_enable` 为必需 sc_out<sc_uint<32>>，所有信号按 width 裁剪。方向位 1 表示输出；outputs=output_latch & direction，output_enable=direction，inputs 读值仅包含输入方向位。上层负责真实引脚合并/电气解析，不能把两个 sc_out 直接接同一个单写者信号。

request_drain 只关闭新 MMIO，已接收访问完成，输入边沿采样继续；idle 指 MMIO 无在途。reset 清输出/方向/中断状态、取消旧 MMIO，以当前输入为基线，避免高电平在撤销 reset 后形成假边沿。撤销后显式 resume。

等待 idle_event 时先检查 idle()；IRQ/信号经 delta-cycle 传播后观察。顶层持有 sc_main、停止策略、时间精度。系统热复位前先 drain host/bus；本模型可取消并不意味着整条互连支持热复位。

可直接复现的 source/install/move 命令与地址映射见 [独立消费者](../../../examples/peripheral_system/README.md)。依赖公共源码只用于构建，安装消费者只包含公开头文件和包。
