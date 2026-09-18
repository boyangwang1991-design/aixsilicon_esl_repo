# banked_sram：后续性能参考：多 bank SRAM（规划资产）

可用状态唯一由 [registry.yaml](../../registry.yaml) 的 `aixsilicon:esl:banked_sram:0.1.0` 管理，目前登记为 `planned`。本目录是设计任务，不包含可运行的 SystemC 实现或可用 profile；不得作为 available 装配。

## 目标与接口

- SystemC 模型主体：地址交织、有限 bank 端口、仲裁/Router/响应背压和分片重组。
- 拟定端口：TLM target（多端口按声明），内部 bank 服务。
- 第一版边界：第二阶段；不以基础 RAM 的固定延迟声称 bank 性能。
- 依赖：ram 存储组件、tlm_bus 资源策略、Observe。
- Python 算法参考：默认不需要；用独立已知值/状态不变量/解析预期检查，无算法时不维护 Python 镜像。

## 接入基础系统

通过公开 SystemC 库和端口接入 [基础系统规划](../../examples/basic_system/README.md)。具体地址由上层系统分配，不硬编码系统绝对地址；内存映射端口接收局部地址。模型只提供模块/配置/事务行为，上层持有 sc_main、全局时间分辨率和停止条件。真实厂商 ABI 兼容须另有规格支撑。

## 实现时交付的资产

本目录将提供 model.yaml、CMakeLists.txt、systemc/include/banked_sram/、systemc/src/、docs/design.md、docs/integration.md、docs/verification.md、tests/systemc/ 和 examples/integration/。算法参考仅按上面的实际需要添加。实现后公开 CMake target/API、安装包和 source-tree/install-tree 消费者在本模型文档记录；当前不生成空源码或假的 factory/profile。

## 最小验证与参考价值

- 独立行为检查：同/异 bank、热点/HOL、返回背压、字节守恒；M0–M8。
- 集成：独立上层消费者构建、真实事务、多实例、适用的背压/reset/drain；目标时间与数据一致。
- 可供新模型参考：从简单存储到局部精细性能 profile 的演进。
- 当前只是规划参考；实现并附验证证据后，才可作为经过验证的代码参考。

任务顺序和完成证据在 [资产开发账本](../../docs/esl_todo.md) 更新；本页不另维护第二份进度表。
