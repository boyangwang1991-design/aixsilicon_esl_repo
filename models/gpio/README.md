# gpio：基础 GPIO 外设（规划资产）

可用状态唯一由 [registry.yaml](../../registry.yaml) 的 `aixsilicon:esl:gpio:0.1.0` 管理，目前登记为 `planned`。本目录是设计任务，不包含可运行的 SystemC 实现或可用 profile；不得作为 available 装配。

## 目标与接口

- SystemC 模型主体：方向/输出值/输入采样、mask/pending/clear；声明边沿或电平能力。
- 拟定端口：MMIO TLM target、sc_signal 输入/输出、IRQ。
- 第一版边界：数字行为，不模拟电气特性、去抖或跨时钟同步。
- 依赖：common RegBank/IRQ。
- Python 算法参考：默认不需要；用独立已知值/状态不变量/解析预期检查，无算法时不维护 Python 镜像。

## 接入基础系统

通过公开 SystemC 库和端口接入 [基础系统规划](../../examples/basic_system/README.md)。具体地址由上层系统分配，不硬编码系统绝对地址；内存映射端口接收局部地址。模型只提供模块/配置/事务行为，上层持有 sc_main、全局时间分辨率和停止条件。真实厂商 ABI 兼容须另有规格支撑。

## 实现时交付的资产

本目录将提供 model.yaml、CMakeLists.txt、systemc/include/gpio/、systemc/src/、docs/design.md、docs/integration.md、docs/verification.md、tests/systemc/ 和 examples/integration/。算法参考仅按上面的实际需要添加。实现后公开 CMake target/API、安装包和 source-tree/install-tree 消费者在本模型文档记录；当前不生成空源码或假的 factory/profile。

## 最小验证与参考价值

- 独立行为检查：方向切换、输出读回、输入触发、mask/reset、未绑定策略。
- 集成：独立上层消费者构建、真实事务、多实例、适用的背压/reset/drain；目标时间与数据一致。
- 可供新模型参考：简单信号端口与 MMIO 混合集成。
- 当前只是规划参考；实现并附验证证据后，才可作为经过验证的代码参考。

任务顺序和完成证据在 [资产开发账本](../../docs/esl_todo.md) 更新；本页不另维护第二份进度表。
