# 标准集成覆盖说明

此处对应 I01–I07 的工程入口与缺口；不维护第二份任务状态表。运行结论及 hash 以 registry 引用的 run 为准，方法原则属于 Skill。

| 项目 | 当前证据入口 | 尚不能据此声称的能力 |
|---|---|---|
| I01 外部源码消费者 | basic_system / interrupt_system / peripheral_system / dma_system，以及生成模板的 integration | 任意第三方系统自动装配 |
| I02 安装与搬迁 | validate_basic_models；模板验证额外隐藏生成源目录；安装 Markdown 链接检查 | 任意编译器二进制 ABI 互换 |
| I03 独立/共享实例 | RAM 双实例、两个 host/两个 timer、共享 RAM 竞争；模板双寄存器；双 UART/GPIO 的不同配置与状态；双 DMA 的同 ID/不同配置与共享 RAM | 独立双 bus、双 irq_controller 尚无专门用例（NOT_RUN） |
| I04 合法/非法访问 | RAM/系统负向用例；CLI 合同负向测试；模板地址与缺失端口 | 各模型逐个必需端口缺失组合尚未穷举 |
| I05 生命周期 | RAM 在途 reset/drain；timer/IRQ 在途 reset；基础系统 drain/resume；UART 外部背压/阻塞帧 reset；UART/GPIO 在途 MMIO reset/drain/capacity；DMA 命令/完成容量、读/写阶段 reset、drain 与部分错误 | bus/IRQ 在途 drain 的专门压力测试仍缺；同步模板无在途事务，取消项 NOT_APPLICABLE |
| I06 时间/行为 | RAM functional/resource 与 counters_off；总线 delay；timer deadline/W1C；模板 annotated delay；UART 帧时间/overflow W1C 与 GPIO 边沿/W1C；DMA 块服务时间与独立 annotated target | 全库 profile 差分与公平性证明 |
| I07 可复现交付 | 实际命令、源 hash、独立场景/执行次数、安装文档检查；CLI evidence 校验 | 没有运行记录的独立 checkout 不继承本地 PASS |

available 表示当前 README/manifest 范围可构建和运行；不能直接换算成 I01–I07 全项通过。完整标准集成宣称必须补齐适用缺口。工具字段/文件校验通过也不等于模型内部语义已静态证明。
