# 验证范围与证据

统一入口：`esl npu-sram validate --output <fresh-directory>`。状态基于生成的 checks.json 和 CTest 日志，本文不以文件存在宣称通过。

| 检查 | 独立依据 |
|---|---|
| mapping | 32 KiB 全地址枚举：modulo/XOR/contiguous、5 个 stripe、group/local XOR；region 正逆映射 |
| data | 固定 seed 随机地址/data/mask，独立逻辑地址逐字节 oracle |
| timing | 无竞争单 32 B 读精确完成边界 10 cycles |
| backpressure | ROB=1、completion=1，停止消费后服务停止，恢复后有序 drain |
| ecc | 部分 codeword 写无丢失更新、RMW 服务，以及双错响应 |
| hierarchy | 窄 remote 链路下同一逐字节 oracle |
| ordering | 同 ID 多 transaction 返回严格有序 |
| negative | 4 KiB 越界和非法 Bank 数，在接受前拒绝 |
| reset | 在途拒绝 reset，drain 后清存储与统计 |
| pipeline | latency=8、II=3 的 8 次服务精确完成于 16+3i cycles |
| partial_race | 两端口同 codeword 的互补 mask 并发写无丢失更新，单错修复只发生一次 |
| dual | 相同读写负载下 1R1W 双入口比 1RW 完成更早 |
| hol | 构造热点队头阻塞，VOQ 比 FIFO 更早完成且 FIFO 记录 HOL |
| aw_w | W 先于 AW 不提前服务；缺少 W 不阻塞同 ID 的独立读通道 |
| independent consumer | 公开 target 构建，两个不同 mapping 实例、数据隔离、copy ownership、drain/reset |

消费者在 source、install、relocated prefix 各执行一次。relocation 复制安装树后重新配置消费者；不依赖原 build tree 的模型 include/library 路径。

每次性能 run 检查 transaction/fragment 守恒、队列容量、独立 workload payload 总量以及 Bank 服务上界。Python 生成的 expected byte totals 与 C++ runner 从 task 行独立累加的总量交叉检查；未实现数学 GEMM/attention 数值运算，因此不得称 NPU 数值正确性通过。ECC 是性能故障抽象，不是 SECDED 编码器验证。

实验报告的完整窗口是从 workload release 到输出完成并 drain。短算子的 p99 有样本数；不将其当作稳态尾延迟保证。Bank 热力图和 transaction 时间线使用有上限 trace，标题标明窗口；完整指标独立于 trace 收集。

仍需后续校准/扩展的项目：真实 RTL AXI 时间对齐、bit-level ECC、真实 macro write-enable、独立向量计算单元/PE 本地端口、多时钟、完整 QoS/write-drain、scrub 截止期、支持 pin-level 错误恢复。架构提案目标 ≥90% 无冲突峰值、≥10% NPU 提速和最坏退化约束需由实际报告逐项评估；不因功能用例通过而自动满足。
