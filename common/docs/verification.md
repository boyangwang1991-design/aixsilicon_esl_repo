# 公共组件 SystemC 验证

独立 fixture：examples/common_primitives/systemc/main.cpp；13 个 C++/SystemC 场景。source/install/relocated 重复执行，不能把三次同场景计为三个独立场景。

| 判据 | CTest |
|---|---|
| FIFO/满空/拒绝不丢数据/实例独立 | common_queue |
| clear 精确丢弃数、再用与空 front | common_queue_clear |
| 谓词循环与数据/空间 delta 通知 | common_queue_events |
| 复制/赋值异常不消耗队列记录 | common_queue_copy_error |
| 容量/关闭准入/排空/下溢保护 | common_gate |
| RAII 异常路径、失败租约、重复 release | common_gate_exception |
| idle 唤醒及恢复 | common_gate_events |
| 初始化、字节使能、实例独立及清零 | common_store |
| 边界/溢出/空指针/非法使能拒绝原子性 | common_store_errors |
| 解析占用积分、最终 tick、观测不改时间 | common_metrics |
| 同一负载在关闭统计后数据/终态/时间相同 | common_metrics_off |
| 积分溢出饱和标记 | common_overflow |
| 零容量/初始映像超长 | common_invalid |

独立预期：占用 1 持续 2 ns、占用 2 持续 3 ns、占用 1 持续 4 ns，积分应为 12 ns 对应的 entry-ticks，elapsed 为 9 ns。边界、FIFO、使能值均由已知常量判定，不使用被测函数构建 oracle。

真实消费者回归包括 RAM/ROM、host/bus、timer/IRQ、UART/GPIO 和 DMA；它们证明公共实现替换没有改变原数据/时间/取消合同。I01/I02/I07 由独立 CMake/安装/搬迁和证据检查覆盖；I03/I04/I05/I06 由上表与消费者覆盖。组件没有模型端口/profile，未绑定/跨 profile 项不适用；OS 多线程并发、硬件校准和任意二进制 ABI 不在范围。

扩展 fixture 另覆盖：流水 latency/II、并行实例、映射全地址双射与 stride/XOR 热点、RR/WRR/年龄保护、TLM 扩展所有权、非整齐分片与失败汇聚、同流退休、RNG 重放、寄存器 strobe/W1C、SECDED 所有单/双比特组合、DAG buffer/失败传播、链路序列化/credit 延迟、CDC 边沿、稀疏边界、测量/drain/停滞、事件截断、流量重放。加入多 Bank 暂停恢复、两个闭环任务场景和检查器负向注入后，共 39 个独立场景。

组合 fixture 使用固定地址/数据 oracle，分别以不同映射/时序策略访问同一 ByteStore。common_explore 进一步对八组映射/latency/II 配置比较 off/trace 完成时间，保留失败，不把不同策略的速度差解释为功能差。分析脚本的单元测试只验证文件协议及离线统计，不替代 SystemC 模型测试。
