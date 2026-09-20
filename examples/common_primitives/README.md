# common_primitives：公共包独立消费者

仅链接 AixEslCommon / aix::esl::common，不依赖任何具体模型。47 个 SystemC 场景验证基础组件、资源与服务、闭环任务和多 Bank 组合，见 [验证映射](../../common/docs/verification.md)。构建/安装命令见 [集成](../../common/docs/integration.md)。

所有 oracle 在 C++ 中使用已知字节、状态及解析时间；Python 只编排构建/CTest 和证据记录。每用例 1 us 仿真上限、15 s 宿主超时。源码、安装及 prefix 搬迁分别运行相同场景；tools/validate_basic_models.py 同时回归真实模型消费者。

`common_task_pipeline 64 pass` 将 DAG、计算资源、有限 load/store 链路组合起来，按独立解析预期在 13 ns 完成；`64 fail` 验证依赖取消且独立分支继续。`common_composition stripe off 8 1 fault` 注入 Bank0 在 [4,7) ns 暂停，最终数据和守恒必须仍通过。仅用于这些明确配置的公共 fixture，不是通用 SoC 装配器。
