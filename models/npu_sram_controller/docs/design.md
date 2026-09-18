# 实现合同与精度边界

## 时间与功能

SystemC sc_module 每 1 ns 调用一次统一周期推进。阶段为到期事件→网络到达→ECC→Bank→返回派发→请求派发→前端展开→scrub。阶段间通过有期限事件/队列交接，网络包每 hop 至少 1 cycle；相邻 hop 不在同一周期组合穿越。空闲周期当前不跳跃。frontend 在接受后下一拍展开，fragment 再下一拍有资格派发；flat 两级、hierarchical 本地两级/远端五级。link 首片 latency=P，序列化 S=ceil(bytes/width)，完整到达是 t+P+S−1。

Bank latency 与 II 独立；读在 issue 取样，写在 issue+write_latency 提交。同 word 未提交写阻止后续冲突服务；1R1W 同周期不能同时服务相同 word。功能存储用物理 Bank/local 索引，独立测试的 oracle 用逻辑地址。traffic_only 省略数组，仍保留 mask、拆分、RMW 和依赖。

核心代码分别位于 mapping.cpp（合法性/正逆映射/拆分）、fabric.hpp（逐 hop 有限网络）、model.cpp（前端/Bank/ECC/顺序）、runner.cpp（闭环 DAG 驱动）。无跨 beat 合并、广播或 cache。fragment 覆盖一个 Bank word，可以带多个 codeword。

## 配置

`Config` 是公共 C++ 配置类型；`Config::validate` 与 `Config::read` 是实际可运行的校验入口。Python YAML wrapper 严格拒绝未知字段/类型，落盘完整 key=value 配置，C++ 再做容量/整除/接口校验。完整默认值在 tools/npu_explore.py DEFAULTS 与 config.hpp；测试检查两者保持一致。

mapping 支持 modulo、xor、contiguous、region；xor_shift、group_first、local_xor 可组合。region 含独立 Bank 集合、local base、stripe、rotation，要求逻辑/物理范围无重叠。word 和 stripe 独立；拆分按真实物理 word 聚合。地址错误在请求接受前拒绝，4 KiB 检查不允许静默切成另一种协议。

1RW 默认读写共享 Bank 启动能力；dual_port=1 允许分开读写 II，dual_ingress=1 同时扩大 Bank 接收槽。network read/write 是独立输入，目的 Bank/group 命令预算共享。flat 每端口 lanes、128 B 写出/return_bytes 读入；hierarchical 的源上行、交换输出、目的下行各有独立 remote_bytes 预算。request headers 以有限命令槽计数，payload bytes 统计不包含 header；不得将 payload 统计称为全部线网位活动。

每 hop 等待项/预留项和数据字节有限，store-and-forward 先预留下一 hop 容量。请求在进入网络前计入目标 Bank 接收 credit（transit+bank_queue），是保守的端到端目的 credit 策略；回包前先有完整 beat 重组槽，Bank issue 前预留 completion 槽。网络 credit 释放在周期边界可见；credit_delay 参数目前影响 Bank completion 和 ROB 的归还，不额外覆盖每 hop credit 往返。

## 前端、队列和仲裁

submit 分别接纳 AR/AW；每端口每方向每周期一个地址。push_w 是独立事件、最多一个/周期，可先于 AW，关联遵从 AW 到达顺序。W FIFO 的数据在全部 fragment 送入有限下一级后释放。无 WID。beat size=8/16/32/64/128 B、对齐 INCR、最多 32 beats；direct API 的 data/mask 以有效 beat 的紧凑字节索引表示，非 pin-level 1024-bit lane 格式。

每个 port+direction+ID 只有队头 transaction 展开，直到其 RLAST/B 消费才启动后继；不同 ID 可独立返回。read beat 顺序固定，响应按 R/B 消费分别释放资源。未发齐 fragment 的 beat 阻止同一 transaction 后续 beat 展开。outstanding 包含全部已接纳事务，并同时承担地址等待和 B-token 状态预算；本版本不额外提供独立 AR/AW/B depth 参数。

FIFO/Bank VOQ/group VOQ 使用同一 ingress 池。请求源优先顺序按 cycle 旋转，Bank 基线 RR，支持 oldest-ready、年龄保护和 read-first。weighted 使用固定 2:1 权重票据（前半端口为 2、后半为 1），不是任意权重表；不能据此声明任意 QoS 份额保证。matching_rounds 限制链路仲裁扫描；不使用最大匹配 oracle。age_guard 保护只适用于资源/依赖均 eligible 请求。

## ECC、部分写和 scrub

ECC 资源支持 8/16 B codeword、整数 lane、II、encode/decode latency，编码和解码独立。默认每 Bank 引擎；ecc_group 聚合实例并限制每方向本地供数为 256 B/cycle、另加两拍本地传递延迟，是聚合局部服务模型，不是完整逐链路 ECC 交换网。RMW context、Bank completion 和源队列提供有界在途状态。

完整 codeword 写直接 encode→write；部分 codeword 写执行 read/decode/encode/write，锁住相关 codeword。macro_word_write=1 将更新保护扩到整个 word，ECC none 也可能 RMW。writeback 有独立、由 RMW context 限制的保留队列，不能被等待其锁的普通 FIFO 堵住。全零 mask 正常完成，不访问 Bank。

inject(address,1/2) 是显式性能故障状态：单错产生 correction latency 和修复读改写，双错产生 error/poison，不实现 Hamming parity 位计算/物理 bit flip。correctable 修复与正常部分写共计 rmw，不能将该指标全解释为用户 partial-write。scrub 周期性产生内部低优先级读，队列满时暂缓；当前没有 overdue 截止期保证。stop_scrub 停止新内部流量，已接纳请求继续完成。

## 生命周期与局限

submit=0 / push_w=false 表示未接纳，可重试。非法配置/请求抛出异常；SLVERR 由 response.error 表达。reset 仅在 idle/drained 时接受，清空存储、统计和局部周期；仿真全局 SystemC 时间不倒退。observer 只读，trace_limit 只截断 trace，完整指标始终收集。仿真 cycle_limit 与外层 wall timeout 防止无限运行。

本版本未提供：AXI pin adapter/TLM nb_transport bridge、FIXED/WRAP/exclusive、运行中 reset/cancel、检查点、多时钟、mesh、多级 NoC、任意 XOR 矩阵、独立 group stripe、任意 WRR 表、write-drain 水位和 burst lock。它们仍是架构提案中的扩展空间，未知配置字段被拒绝。未提供 RTL 校准。

## NPU 行为范围

Python 生成 tensor 地址与 topological DAG，C++ runner 根据实际完成释放依赖，支持有限 A/B ping-pong 及 C store 生命周期（用依赖编码）。N1–N8 覆盖 GEMM/低并行、在线 tiled attention/decode append、direct convolution、embedding 索引依赖、transpose 和并发预取/回收。

当前 NPU 是声明假设下的 traffic model：PE wave 计算与 softmax 各阶段在每端口同一计算服务中串行，未提供独立向量引擎并行；本地 buffer 容量由生成器核算，尚未模拟逐周期 PE 本地端口争用。R 消费基线为一 beat/port/cycle，W 由 DMA 队列逐 beat 生成；额外 READY 退化在单元测试驱动。embedding 保留预生成索引序列及依赖，不根据 SRAM 返回数值重新解码索引。因此不能用该模型声称数据依赖数值计算已验证。

资源 proxy 为显式容量/交换规模估计，包含链接队列、上下游状态；未经过综合网表或 macro 校准。输出 dependency_wait_tile_cycles 包含 NPU DAG 的输入、输出槽、前驱等待，**不能等同纯 SRAM memory-stall**。frontend_stall 是接纳/展开失败尝试数，不是互斥等待周期。
