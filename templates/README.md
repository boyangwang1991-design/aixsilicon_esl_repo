# SystemC 库模板

register_target 是本轮验证的 32-bit 寄存器库模板；状态以 registry 为准。生成入口：`esl new model demo --template register_target --output /new/path`。生成物包含公开 Model/Config、manifest、设计/集成/验证文档和可复用消费者。

模板是有明确行为的起点，不是任意外设的完整实现。其语义为调用时更新状态并累加 annotated delay；不能当成完成时可见或周期精确模型。

compute_cpp 及 reference/legacy_python 中的历史模板不登记 available，CLI 拒绝生成。工程模板回归入口 tools/validate_register_template.py，在隔离候选 registry 中生成后验证，再决定是否登记；不会为运行测试临时升级生产 registry 状态。
