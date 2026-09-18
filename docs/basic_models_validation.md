# 基础模型验证记录

统一执行入口 tools/validate_basic_models.py。当前证据：`runs/review-fixes-integration-final/checks.json`，保存逐步命令/退出码/日志与源码 SHA-256（含根 CMake 和验证脚本）。这仍是本地 dirty 开发验证，不是干净发布基线。

本次共 28 个独立用例、68 次 CTest 执行：RAM 8 项；B0 8 项与 B1 中断子集 12 项，分别在源码消费、安装消费、移动 prefix 后重新消费执行。全部 PASS。导出包扫描未发现源树绝对路径泄漏；安装后的 Markdown 本地文档链接完整，MMIO32 公共合同已随包交付。逐模型已知缺项见 [集成覆盖](integration_coverage.md)。

register_target 模板另有独立生成、源码消费、隐藏生成源码后安装消费、prefix 搬迁消费验证，证据为 `runs/review-fixes-template-validated/checks.json`；两个独立用例在三种消费者模式执行，共 6 次 CTest。模板验证不增加基础模型的覆盖项。

B1 覆盖：单次/周期到期、重编程、同刻 W1C set 优先、mask/unmask/最低编号优先级/电平重挂起、双实例独立性、在途访问 reset 取消与恢复、已挂起 IRQ reset 清除、容量拒绝、非法寄存器访问、delay 不重复计费、drain/resume、未绑定端口和非法配置。公共 MMIO 约束见 contracts/mmio32.md。

环境：SystemC 3.0.2（package 3.0.2.20251031）、C++17、GCC 8.5.0、CMake 4.4.3。仅证明记录环境中的源码重编译与安装消费；未证明跨编译器 ABI、AT/DMI、CPU 软件启动、厂商寄存器兼容或硬件性能校准。

B1 目前仅 timer/irq_controller 完成；UART/GPIO 与 B2 DMA 仍 planned。host/bus 仍采用 drain-only 的系统复位前置协议。历史 B0 证据保留于 runs/layout-refactor-20260918，不覆盖旧结果。
