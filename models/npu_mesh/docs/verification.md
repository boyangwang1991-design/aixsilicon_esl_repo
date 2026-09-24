# 验证边界与入口

`esl npu-mesh validate --output <new-dir>` 构建真实 SystemC，运行 CTest，
独立源码消费者、安装搬迁消费者，以及六种 full_data BM 的 Python oracle。
各次运行的 checks.json/run.json、simulation.log、completions.tsv 是判据；
报告摘要不能替代原始证据，源码变化后需重跑。

| 用例 | 判据 |
|---|---|
| data / split / central | 跨4KB、519B、稀疏 mask，写后读字节相等，实例隔离 |
| ordering | 同 ID 顺序、fence 不早完、先前失败传播 |
| backpressure | tracker 满 RETRY、慢目标下冷目标前进、最终不丢、credit stall |
| dma / multicast | 独立固定向量、strided/快照/overlap拒绝、源片只读一次、全部目的可见 |
| reset / timeout | ABORTED/TIMEOUT_UNCERTAIN、排空、恢复后的新事务成功 |
| token | 无 reservation 拒绝、禁止提前复用、失败 poison |
| command | 数据受阻时独立消息可达、事件去重/旧 epoch 拒绝 |
| config | 零深度、重叠映射、地址溢出拒绝 |
| axi | W-before-AW 反压、AW/W 分离、窄非对齐 mask、RID/RLAST、错误 R/B 完整终结 |

Python oracle 不调用 DUT，逐字节执行无数据竞争 DAG，核对 read 返回与最终全部 memory。
同地址存在写且无 dependency 的输入在运行前拒绝，避免 oracle 凭输入次序制造全局顺序。
校验数据/终态、依赖时间、tracker、buffer 上限、物理 link 容量上界、完成字节数。
网络每时钟独立检查所有 credit 守恒；这些不是 unbounded deadlock 证明。

I01 源码消费者、I02 安装后搬迁消费者、I03 双实例、I04 配置/范围错误、I05 背压和 reset/drain、
I06 shared/split 数据一致属于本版验证范围。未验证商业虚拟平台 ABI、引脚 AXI、CDC/RDC、
RTL 周期误差、真实模型数值与真实 BM trace。性能只支持未标定资源模型内的相对比较。

新增定量与生命周期用例：network_bounds 穷举 8×8 src/dst，核对 Manhattan 跳数×编码 flit 字节，
单链路 64×256B 长流对比 depth=1/16、credit_latency=8 与 width=32/64B；断言带宽上界及深缓冲
稳态效率至少 80%。timeout_tail 覆盖 head 已占目标槽后超时，迟到 tail 必须回收预留；
dma_cancel 要求在途读真实排空后 abort；management 检查在途 map 更新拒绝与 drain 后提交。

存储/窗口增量在 `tests/systemc/test_memory.cpp`：sram_data 检查非对齐/mask/邻接字节与同一
调试存储；sram_time 验证 DDR 延迟不被重复计入；window 比较并发与串行；bank 对比同bank
热点与分散写；ddr 核对读写共享带宽和切换影响；multicast_large 检查8KB三目的只读8192B。
reset_native/timeout_native 要求已接受AW排空完整W再结束，读回实际写效果；partial/cancel/
dma_reset 在目的已产生写效果后触发，核对每目的已写字节；slow_fanout 检查慢接收者下不能
提前完成/释放token；reservation 验证未消费读完成仍保留返回预留。CLI validate 同时运行
原 SRAM 14个模块用例、独立安装消费者和七类BM，具体计数与结果以本次 checks.json 为准。

干扰实验额外验证固定Decode/初始化cohort、独立oracle无竞争、单片trace阶段守恒及统计口径。
所有BM检查release <= accepted <= done；runner使用模型周期判断release，避免外层循环相位偏差。
