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

扩展 fixture 另覆盖：流水 latency/II、并行实例、映射全地址双射与 stride/XOR 热点、RR/WRR/年龄保护、TLM 扩展所有权、非整齐分片与失败汇聚、同流退休、RNG 重放、寄存器 strobe/W1C、SECDED 所有单/双比特组合、DAG buffer/失败传播、链路序列化/credit 延迟、CDC 边沿、稀疏边界、测量/drain/停滞、事件截断、流量重放。加入多 Bank 暂停恢复、两个闭环任务场景和检查器负向注入后，共 46 个独立场景。

组合 fixture 使用固定地址/数据 oracle，分别以不同映射/时序策略访问同一 ByteStore。common_explore 进一步对八组映射/latency/II 配置比较 off/trace 完成时间，保留失败，不把不同策略的速度差解释为功能差。分析脚本的单元测试只验证文件协议及离线统计，不替代 SystemC 模型测试。

新增存储可靠性场景：`common_storage_dense/sparse` 对同一 burst/mask/跨页/非法请求合同和 ECC RMW/scrub 运行独立 oracle；`common_storage_timed_dense/timed_sparse/timed_baseline` 以实际 SystemC 共享 bank/codec 对照 12 ns/7 ns，检查资源排空和最终数据。新增策略场景 `common_policy_compute` 验证尾部工作量取整、溢出、latency 与 II 及完成 credit，`common_policy_interrupts` 验证边沿/电平、同采样 W1C 优先级、mask 和门限。task_pipeline 同时作为 ComputeTiming 的第二个消费者，解析完成时间保持 13 ns。

工作负载 fixture `common_workload_trace` 验证读写 roundtrip、clock/ID/依赖/hex/mask/溢出拒绝，共 47 个公共场景。第二消费者 systems/multibank 在实际 SystemC 中生成并回放，配置/依赖/观测与失败场景由 tools/validate_multibank.py 独立验证；Python 不推进仿真时间。

新增 `common_credit_pipeline_{1,2}_{on,off}` 四个组合场景，公共场景总数为 51。
输入 FIFO、全程 Gate、流水资源与输出 FIFO 使用独立容量，逐阶段检查 credit、队列计数和
payload 所有权守恒。检查 latency/II 独立、已完成未消费不归还容量、下游暂停后恢复、
输出满时结果保留、数据与顺序、提前/重复退休及活动 reset 拒绝、drain 后复用、历史水位。
解析时刻见公共合同；不从被测输出推导预期。统计 off 同样验证业务守恒。

`common_statistics_on/off/overflow` 新增三个独立场景，公共场景共 54 个；覆盖零窗口、尾区间、重叠 span、非法/未来区间、零长样本、溢出饱和与观测关闭。credit_pipeline 同时作为第二消费者，以 33 ns 积分/总 span 和 18 ns 忙时验证聚合口径。

`common_regions_bijection/dense/sparse/errors/wide` 新增五个区域表场景，公共场景共 59 个。固定布线 oracle、全覆盖唯一性/逆映射、逻辑与物理空洞、非法表及 uint64 边界均验证；两个存储后端核对每个物理字节和 mask。NPU mapping fixture 另外保留全地址双射并增加固定向量、空洞及逻辑/物理别名拒绝，证明实际模型消费者复用。

`common_bursts_schedule/errors/closed_loop/random_replay` 新增四个独立场景，公共场景共 63 个；覆盖部分尾批、相位、零周期、uint64 边界/溢出、20-byte 有限闭环与背压重试、随机读写计划不变和 trace 往返。multibank 的 bursts.yaml 以 23-cycle 独立解析预期验证真实消费者，并覆盖双端口相位、观测开关、回放及预算失败。

`common_fault_service_{baseline,pause,zero,error,delay,bandwidth,combined,retirement,seeded,overflow}`
及 `common_fault_service_combined_off` 新增 11 个场景，公共场景共 74 个。
每个故障实际影响准入/完成/数据提交，使用独立固定准入时刻与失败字节 oracle；
源/安装/搬迁重复运行，不重复计独立场景。原 common_composition 的六种配置改由
FaultService 预留资源，并保留原数据、II 对照和 Bank 暂停场景；其余模型未自动扩大能力。
