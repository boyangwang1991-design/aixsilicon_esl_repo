# rom 验证

运行仓库 `tools/validate_basic_models.py`，统一构建 RAM 单元测试与 basic_system 消费者，执行源码树、安装包和移动安装目录三种测试。输出 checks.json、逐步日志、源文件 SHA-256；结果按实际执行判定，不以目录存在代替通过。

RAM 专项覆盖读写/字节使能、functional/resource 时间、观测关闭、双实例独立性、有限容量、在途 reset/drain、错误与必需端口。组合覆盖 ROM 映像与写保护、双 host 共享 RAM、译码/跨界、drain/resume、重叠窗口和缺失目标绑定。

限制：只验证当前 GCC 8.5 / C++17 / SystemC 3.0.2 环境的源码重编译与同环境安装消费；未验证跨编译器 ABI、AT、DMI、IRQ、CPU 启动、硬件性能校准或总线仲裁公平性。最新执行入口和证据见 [B0 记录](../../../docs/basic_models_validation.md)。
