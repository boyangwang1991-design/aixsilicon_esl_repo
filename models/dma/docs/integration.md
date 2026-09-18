# DMA 上层集成

源码方式：`add_subdirectory(${ESL_MODELS_SOURCE_DIR}/models/dma models/dma)`。安装方式：`find_package(AixEslDma 0.1 CONFIG REQUIRED)`。两者都链接 `aix::esl::dma`，包含 `<dma/model.hpp>`。依赖 C++17/SystemC 3.0.2，公共头文件不引用仓库私有 helper；完整构建命令见 [消费者](../../../examples/dma_system/README.md)。

构造 `aix::esl::dma::Model("dma0", config)`。必需端口 `memory` 为 32-bit `simple_initiator_socket<Model,32>`，标准 TLM-2 generic payload、LT blocking，64-bit 全局字节地址，上层可接总线或直接 memory target。仅恰好一个 target；未绑定在 elaboration 报错。数据是逐字节搬运，与宿主整数端序无关；data_length=streaming_width=当前块长，byte-enable 为空，目标必须支持对应长度/地址，若不支持则按错误完成。socket bus width 不意味着 burst 只能 4 字节。

公开命令 API：`submit(Command)`、`pop_completion(Completion&)`、`completion_event()`；生命周期：`idle()`、`idle_event()`、`request_drain()`、`reset()`、`resume()`。仅从 SystemC 仿真线程/方法或仿真前配置阶段调用，不是 OS 线程安全 API。submit/pop/reset 本身不 wait；后台 SC_THREAD 执行下游事务。无 IRQ/reset 信号或 MMIO socket，不绑定虚构端口。

示例消费者先由 host 初始化 RAM，再提交 command；循环 pop，若无结果再等待 completion_event，并按 ID/status/bytes_written/response 检查。满容量应取走完成记录后再重试 submit；下游错误是否重试由上层决定，不能默认一个 GENERIC_ERROR 一定可以安全重试。

块可见性、容量及复位约定见 [设计](design.md)。先关闭 DMA 和主机上游提交，待 DMA idle 后再 drain 总线/RAM，避免阻塞已接受任务。顶层持有 sc_main/sc_start/时间精度/watchdog；模型不停止全局仿真。该交付证明标准源码/安装集成，不承诺任意工具链 ABI、CPU 启动或商用 DMA 软件兼容。
