# 基础 IP 模型规划索引

状态唯一见 [registry.yaml](../registry.yaml)，RAM、ROM、host_master、tlm_bus 已实现 B0；timer、irq_controller、uart、gpio 已有 B1 消费者；DMA 已有 B2 消费者；其余三类保持 planned。模型细节只在各自 README 维护；SystemC 为主体，可选 Python 仅明确算法行为。此处列组织关系，不把目录存在或 Python 原型当成实现完成。

- [host_master](../models/host_master/README.md)
- [tlm_bus](../models/tlm_bus/README.md)
- [ram](../models/ram/README.md)
- [rom](../models/rom/README.md)
- [dma](../models/dma/README.md)
- [compute](../models/compute/README.md)
- [bmu](../models/bmu/README.md)
- [timer](../models/timer/README.md)
- [irq_controller](../models/irq_controller/README.md)
- [uart](../models/uart/README.md)
- [gpio](../models/gpio/README.md)
- [banked_sram](../models/banked_sram/README.md)

基础系统装配、地址图与验收阶段唯一见 [basic_system](../examples/basic_system/README.md)。最小可运行子集是 host_master/tlm_bus/ram/rom；先打通 B0，再加 B1 外设与 B2 DMA，算法/BMU 与 bank 模型按需扩展。

这些资产同时作为后续模型的参考：九个模型提供可复用代码与外部消费者；其余模型仅供规划参考。验证范围见 [B0 验证记录](basic_models_validation.md)。DMA 已使用四段 ID，并在 registry 中保留五段发现别名；compute 等其余历史 ID 在首次实现时按 contracts/tool_contracts.md 迁移，不能不经校验生成 manifest。
