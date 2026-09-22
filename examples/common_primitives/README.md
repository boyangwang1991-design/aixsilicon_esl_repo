# common_primitives：公共包独立消费者

仅链接 AixEslCommon / aix::esl::common，不依赖任何具体模型。74 个 SystemC 场景验证基础组件、资源与服务、闭环任务和多 Bank 组合，见 [验证映射](../../common/docs/verification.md)。构建/安装命令见 [集成](../../common/docs/integration.md)。

所有 oracle 在 C++ 中使用已知字节、状态及解析时间；Python 只编排构建/CTest 和证据记录。每用例 1 us 仿真上限、15 s 宿主超时。源码、安装及 prefix 搬迁分别运行相同场景；tools/validate_basic_models.py 同时回归真实模型消费者。

`common_task_pipeline 64 pass` 将 DAG、计算资源、有限 load/store 链路组合起来，按独立解析预期在 13 ns 完成；`64 fail` 验证依赖取消且独立分支继续。`common_composition stripe off 8 1 fault` 注入 Bank0 在 [4,7) ns 暂停，最终数据和守恒必须仍通过。仅用于这些明确配置的公共 fixture，不是通用 SoC 装配器。

## Credit 与有限输出组合

`common_credit_pipeline_1_on/1_off/2_on/2_off` 将输入 FIFO、全程 Gate、ResourceTiming
与有限输出 FIFO 组合，逐阶段检查所有权/credit 守恒以及下游背压。输出深度分别为 1/2，
统计开关不改变六个请求的准入与消费时刻，最后一个结果必须在 18 ns 消费。
这些场景随本目录 CTest 执行，并由 `tools/validate_basic_models.py` 覆盖源码/安装/搬迁路径。

统计专项 `common_statistics_on/off/overflow` 验证通用 Counter/Gauge/Span；credit_pipeline 提供第二个实际组合消费者。

区域表专项 `common_regions_bijection/dense/sparse/errors/wide` 覆盖有序 Bank 子集、窗口、旋转、空洞与边界；同一 RegionMapper 也供 NPU 区域模式使用。

突发专项 `common_bursts_schedule/errors/closed_loop/random_replay` 验证有限时间批次、端口相位与背压请求保留；第二消费者为 multibank 的 bursts.yaml。

故障服务专项 `common_fault_service_*` 验证暂停、零带宽、错误、延迟、准入速率下降、
组合效果、credit 生命周期、随机窗口导出重放及溢出。common_composition 复用同一适配器。
