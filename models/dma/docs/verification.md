# DMA SystemC 验证

唯一 fixture 为 [dma_system](../../../examples/dma_system/README.md) 的 C++/SystemC 消费者，同一套测试用于源码、安装和 prefix 搬迁后的独立构建。没有 Python DMA 行为测试。

| 集成项 | CTest 判据 |
|---|---|
| I01/I02/I07 | source/install/relocated 模式执行 dma_*；validate_basic_models 保存实际命令、源码 hash、独立用例数、环境与安装文档检查 |
| I03 | dma_dual：两个不同 burst/capacity 实例使用相同 ID、独立目标；dma_contention：host 与 DMA 共用一个真实 RAM/总线 |
| I04 | dma_invalid/invalid_config/unbound：零/过长/重叠/溢出/重复 ID/未绑定；read_error/write_error/partial_error：真实下游错误和部分进展 |
| I05 | dma_capacity：排队/完成 credit，不丢不重复；drain：已有命令完成及恢复；reset_read/reset_write/reset_queued：取消粒度、payload 保留、恢复；downstream_full：拒绝不隐式重试 |
| I06 | dma_copy/tail：已知字节与精确服务时间、尾部不越界；dma_annotation：独立 annotated-only target，读后写顺序与返回 delay 各消费一次 |

初始数据由 host 按独立常量生成，预期内存/长度/终态由消费者断言，未调用 DMA 计算作为 oracle。只有 command profile，无可开关观测；跨 profile/观测差分不适用。适用范围和当前结果见 [验证记录](../../../docs/basic_models_validation.md)，registry.evidence 是证据索引。

不证明任意下游错误写的原子性、总线公平性、永久阻塞目标的取消、所有 uint32 参数边界或硬件吞吐；目标函数抛异常视为仿真错误，不伪造正常 Completion。MMIO、IRQ、SG/stride/descriptor 后续单独设计验证。
