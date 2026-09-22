# 可复用 SystemC 基础组件

公共包 `AixEslCommon` / `aix::esl::common`，ID `aixsilicon:esl:common:0.1.0`；状态与证据唯一见仓库 registry。公开合同见 [common_primitives](../contracts/common_primitives.md)，文件清单由 common.yaml 管理。

| 组件 | 已接入的消费者 | 公共化的边界 |
|---|---|---|
| BoundedQueue<T> | DMA 命令/完成队列，UART TX/RX | 有限存储/FIFO/变化通知；命令总 credit 和帧服务归模型 |
| BlockingGate/BlockingLease | host_master、tlm_bus、timer/IRQ/UART/GPIO 的 MMIO helper | 准入/释放/排空；不做服务仲裁或强制取消 |
| ByteStore | RAM；ROM 复用同一 RAM 实现 | 字节、边界、byte-enable；无总线时序与只读策略 |
| ActivityMonitor | Queue 与 Gate 的占用和事件计数 | 被动整数 tick 积分；无 trace/日志框架 |

[设计](docs/design.md)、[集成](docs/integration.md)、[验证](docs/verification.md) 分别说明职责、包使用与 C++ 测试。独立消费者位于 examples/common_primitives，模型系统消费者继续负责实际行为回归。没有复制 Python 公共组件，也不以 Python 测试替代 SystemC 验收。

Mmio32 是内部 helper，保持不安装；公共包注册与各组件的完整能力验收分别跟踪，不把部分功能的测试通过视为整个 TODO 完成。

公共架构扩展还提供资源时序、仲裁、地址映射、事务/TLM 适配、分片汇聚、顺序退休、确定性随机流、寄存器/中断、SECDED、闭环 DAG、有限链路、CDC 可见延迟、稀疏存储、生命周期、停滞诊断、流量和事件记录。每项边界以合同为准，未涵盖的完整功能继续由 TODO 跟踪。

独立 fixture 已扩为 main/architecture/services/infrastructure/composition/task_pipeline 六组，含双端口四 Bank 组合。离线实验入口：

```bash
uv run python tools/common_explore.py sweep --executable build/common-source/common_composition --output runs/common-explore
```

输出 results.json、逐点日志/事件和 report.html；8 组配置各执行 off/trace，对比完成时间并检查最终数据。Python 只编排真实 SystemC 程序并离线分析。

存储可靠性扩展：ByteStore/SparseStore 已统一 burst 与 mask 合同；`EccMemory<Storage>` 支持真实 SECDED 部分写 RMW、单字 scrub、纠错/不可纠正错误与精确操作计数。SystemC 组合负责资源预留和可见时刻，详见公共合同。`ComputeTiming` 从工作量/吞吐构造有限计算资源；InterruptState 可按显式采样点区分边沿和电平并处理同刻 W1C。

工作负载补充 `WorkloadTrace`：严格的版本化读写计划，保留字节数据、mask、ID、source、最早注入周期与前驱依赖。观测 trace 不作为回放输入。配置驱动的系统消费者见 systems/multibank。

通用 `Counter/Gauge/Span` 已提供计数、占用/忙时积分与完成区间汇总，详见公共合同。实现已按职责迁至 primitives/infrastructure/adapters/services/workloads/verification；common 保留聚合入口，公开 include 与 target 不变。

公共 `RegionMapper` 提供无别名区域表、Bank 子集/窗口/旋转和正逆映射，由独立存储 fixture 与 NPU 区域模式共同消费；不扩大 NPU 已声明的配置范围。

`BurstSchedule` 将有限请求数按批次周期和相位释放，复用 TrafficSource 地址/读写生成及 WorkloadTrace 录制；它不引入 AXI burst 协议，详细边界见公共合同。

`FaultService` 将 FaultSchedule 的暂停/错误/额外延迟/准入速率效果应用于单发射有限资源；
seeded 窗口只在构造时随机化，查询无副作用，windows() 可导出重放。消费者负责完成时的
错误响应和数据提交，不自动改变未接入模型的故障能力。
