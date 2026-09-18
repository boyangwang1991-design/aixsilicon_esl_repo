# 模板体系（薄骨架，复用厚组件）

边界（见 skill references/boundaries.md §3）：
- 模板 = **薄骨架**：只暴露可插入的配置、功能 hook、时间策略、业务埋点。
- **禁止复制 common 实现**；生成物须能独立构建并执行一个实际小案例。
- 未实现能力明确关闭，不留 "TODO 任意实现" 却算可运行模板。

使用：`esl new model <name> --template <template>`（生成到 models/<name>/）。

## 模板清单

| 模板 | 复用 common | 生成的薄骨架 | 自带测试 |
|---|---|---|---|
| compute | ComputeShell | 功能函数、类型、时间参数 | 独立 oracle + 边界值 |
| compute_cpp | SystemC sc_module | SystemC 计算薄骨架 + CMake | cmake 构建 + 运行 |
| command_engine | Queue、ServiceResource、Event | accept/execute/complete | queue full、完成唯一性 |
| buffer_manager | BufferPool、Event | 分配/释放策略 | 用尽、唤醒、重复释放 |
| memory_target | ByteStore、MemoryPort | 地址/容量配置、响应 hook | 越界、byte enable、数据可见性 |
| pipeline_system | Factory、ScenarioRunner | system.yaml、oracle、README | 端到端最小样例 |
| parameter_sweep | SweepRunner、RunComparator | experiment.yaml、sweep 驱动 | 非法点过滤与基线对照 |
| report | reporting 工具 | 一页摘要 + run 产物 | 报告生成冒烟 |

语言分工（repo-plan §2）：Python 模板做功能 oracle/参考；**C++/SystemC 模板
（compute_cpp）承载并发/事件/资源竞争/目标仿真时间**，是核心目标模型载体。
>>> >>> REPLACE
