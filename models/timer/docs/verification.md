# 验证

统一入口 tools/validate_basic_models.py，同一 interrupt_system CTest 集在源码消费、安装消费和移动 prefix 后重新消费执行。测试包括到期精确时刻、周期与重编程、同刻 W1C、屏蔽/优先级/源未清时重挂起、两个实例、在途 reset、容量拒绝、RO/非法访问、delay 计费、drain/resume、未绑定端口与非法配置。

仅证明当前 SystemC 3.0.2/C++17 环境中的探索 ABI 行为，不证明真实芯片软件/时序兼容。实际命令/hash 证据见 [统一验证记录](../../../docs/basic_models_validation.md)。
