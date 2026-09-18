# NPU 多 AXI SRAM Controller：ESL 架构探索实施规划

日期：2026-09-18。状态：规划；文中默认值为实验起点，非硅实现承诺，未宣称任何仿真或 PPA 结果。

## 1. 目标与决策输出

构建 performance-oriented ESL 模型：8 个 1024-bit AXI4 Slave 接口连接 NPU Tile/DMA，访问多 Bank 共享 SRAM。模型应回答交织粒度、Bank 组织、路由拓扑和缓冲策略如何影响 NPU 算子完成时间、计算停顿、吞吐及尾延迟。

最终交付应是可复现实验及架构选择依据，而非仅有一个能读写的 SRAM 模型。输出：推荐配置及适用负载、较差/对抗负载、资源开销代理指标、待 RTL/物理设计校准的假设、编译器数据布局约束。不得预先认定 XOR 或更多 Bank 必然更优。

## 2. 模型精度与技术方案

建议 Linux、C++/SystemC + TLM-2.0 接入、YAML 配置、Python 实验与报告。依赖版本及编译器必须锁定并记录；具体 SystemC 补丁版本由工程兼容性测试冻结。

采用三层：①不计时功能参考（按字节存储及 mask 更新）；②主要探索用资源/周期模型；③后续对少量候选实现 AXI pin-level 适配与 RTL 对比。②必须建模 beat/fragment 级资源占用、有限队列、反压和完成顺序；不可只给每个 burst 加一个固定 delay。

TLM generic payload 本身不是完整 AXI 模型。定义扩展字段和独立流量事件，表达 AW/AR、W 到达、R/B 消费、ID、burst、size、WSTRB、QoS；禁止把 AW 与全部 W 隐式同时送达。共享仲裁点附近禁止时间解耦越过下一次竞争事件。实现统一 evaluate/commit 两阶段调度，消除同周期端口遍历顺序造成的偏置；空闲时允许事件跳跃。

功能参考与性能路径共享地址语义，但参考模型不共享被测地址译码实现。performance-only 模式可省略大数据数组；保留相同请求切分、mask、ECC、依赖和资源占用。correctness 模式保存数据并做逐字节检查。

## 3. 首先冻结的概念

| 概念 | 定义 | 必须独立配置的原因 |
|---|---|---|
| AXI beat | 外部总线最多 128 B 的一次数据传输 | 窄传输的有效字节可能更少 |
| Physical Bank | 可独立寻址、独立仲裁的 SRAM 服务资源 | 同地址并行拼宽的宏不一定是独立 Bank |
| Bank word | 一次物理 Bank 服务的数据宽度 | 决定拆分数量及 ECC 访问代价 |
| Bank group | 共享局部路由的 Bank 集合 | 表达分层和本地/远端访问 |
| Stripe | 地址交织连续字节块 | 与 beat、Bank word 不必相等 |
| Fragment | 一个 beat 在某一 Bank word 上的子请求 | 是 Bank 服务及完成重组单位 |
| ECC codeword | 独立纠错与更新单元 | 与 Bank word 不必相同 |

AXI 宽度、内部链路宽度、Bank word 宽度、交织粒度四者严禁捆绑为一个参数。

## 4. 带宽预算与初始候选

外部读方向最大为 8×128=1024 B/cycle，写方向也为 1024 B/cycle。1 GHz 时分别为 1.024 TB/s（十进制）。AXI 读写通道独立，不代表后端单端口 SRAM 能同时提供两倍带宽。

设 N 个相同 Bank，每个有效数据宽度 W B，服务启动间隔 II，频率 f。1RW 理想总读写带宽为 N×W×f/II；它还受路由各截面、返回链路、入口和 ECC 吞吐限制。1R1W 应分开预算读写容量，并限制同地址碰撞语义。

| 候选 | Physical Bank | 每 Bank 有效数据宽度 | 1RW、II=1 理想总量 | 目的 |
|---|---:|---:|---:|---|
| A | 8 | 1024 bit | 1024 B/cycle | 低 Bank 数、整 beat 服务对照 |
| B | 16 | 512 bit | 1024 B/cycle | 中等拆分和并行度 |
| C | 32 | 256 bit | 1024 B/cycle | 推荐首个探索基线 |
| D | 64 | 128 bit | 1024 B/cycle | 更细交织及路由开销对照 |

上述为逻辑服务宽度；例如 1024-bit Bank 可能由多个宏锁步拼宽。保持有效 SRAM 总容量一致，例如 8 MiB，另记录 ECC 增加的物理位数。不能把更多并行带宽带来的收益误认为交织收益。另开带宽扩展实验：固定 Bank 宽度增加 Bank 数；单独研究 1R1W。

C 基线：32 B stripe，一个完整对齐 AXI beat 拆为 4 个 Bank fragment。8 个端口同时各发一个 beat 需要 32 次 Bank 服务。只有请求分布互不冲突、路由足够宽且响应及时消费时，才可能达到上述理想吞吐。这里的独立服务资源必须为 32 个，不能把 4 个永远锁步的 lane 误计为自由仲裁的 Bank。

## 5. 组件划分及合同

数据流：NPU workload → 8×AXI front end → beat splitter → address mapper → request/write network → bank scheduler/ECC/SRAM → read response network → reassembly/order buffer → AXI response → NPU dependency release。

| 组件 | 职责 | 关键状态/参数 |
|---|---|---|
| WorkloadEngine | 合成流量、trace、闭环 NPU DAG | release、依赖、buffer token、计算周期 |
| AxiFrontend | AW/AR/W 解耦、burst 展开、入口反压 | ID、R/W outstanding、地址/数据队列 |
| BeatSplitter | 按有效字节、stripe、word、ECC 边界生成 fragment | beat_id、fragment_id、mask |
| AddressMapper | 统一逻辑地址到 group/bank/row/lane | modulo、XOR、region、逆映射 |
| Fabric | 请求、写数据、读返回分别建模 | 图、链路容量、仲裁、credit、pipeline |
| BankScheduler | 冲突仲裁、读写策略、饥饿约束 | 每 Bank 队列、年龄、RMW lock |
| EccService | 编解码吞吐/延迟、RMW、scrub、错误响应 | codeword、实例数、II、注错事件 |
| SramBank | 1RW/1R1W 服务能力和数据提交 | latency、II、同地址 R/W 规则 |
| ResponseAssembler | fragment 完成收集及 AXI 顺序 | ROB 容量、R/B output buffer |
| Monitor/Reporter | 守恒、事件时间线、计数及比较 | run manifest、配置 hash、seed |

内部请求包含 source_port、AXI ID、transaction/beat/fragment ID、逻辑地址、物理位置、操作类型、有效字节 mask、QoS、生成/接收周期。拓扑只消费 mapper 的目的地，不能自行改变地址映射。

## 6. 地址交织设计

对以 region base 为原点的字节地址 a、stripe 大小 G、Bank 数 N：

q=floor(a/G)，offset=a mod G；bank=q mod N；local_byte=floor(q/N)×G+offset。

再由 Bank word 宽度转换为 row/lane。若 G 小于 Bank word，会产生局部 word 的部分访问；必须按实际 fragment/合并规则计费，不可默认它等同整 word 带宽。

当 N=2^k，可支持可逆 XOR：low=q mod N，high=floor(q/N)，bank=low XOR (high & (N−1))，local_byte=high×G+offset。保留 high 后可逆还原 low，避免哈希产生别名。其他 XOR bit-mask 方案必须先证明/枚举验证双射。

必备策略：

1. 连续 Bank 分区：作为局部性/编译器着色对照。
2. 低位 modulo：G=32/64/128/256/512 B，以及 2 KiB 粗粒度对照；依具体 Bank 宽度筛掉无意义组合。
3. 可逆 XOR：至少上述公式，并允许配置高位参与位段。
4. Region mapping：权重、activation、output、KV 等区域分别选择 interleave 或 Bank 子集；区域不重叠且容量可核验。
5. 分层 mapping：同时对比先在 group 内交织再跨 group、以及先跨 group 的方案。

同一共享逻辑地址无论来自哪个 AXI 端口，必须映射到同一物理存储位置。source_port 只能用于路由/仲裁，不能直接加入共享地址哈希。需要端口私有空间时，用明确地址 region 表达。

运行中不允许直接切换已有数据的映射。每次实验重新初始化；若将来支持在线切换，应另实现静默、数据迁移及一致性协议。

必须覆盖：多个端口相同 base 的不同 offset、相同 stride、base 相差 Bank 周期、2 的幂 leading dimension、padding、页/region 边界。Bank 冲突既可能来自 beat 内拆分，也可能来自多个端口；分开统计。

## 7. 路由候选与公平比较

| 拓扑 | 用途 | 不能忽略的代价 |
|---|---|---|
| 理想 fabric | 去除路由限制、保留 Bank/AXI 限制的乐观参照 | 必须标记不可实现，另有完全无冲突理想内存参照 |
| Flat crossbar | 首个真实候选，便于分析交织本身 | 每输入发送能力、输出竞争、返回方向、数据宽度 |
| 4-group hierarchical | 推荐与 flat 并列比较；每组 8 Bank、常态本地访问 | 跨组共享链路、上下行容量、分级队列和额外 pipeline |
| 多级网络 | 检查交换资源压缩后的性能 | 内部阻塞、每级缓冲和路由规则 |
| Packet mesh | 面积距离确有需求时扩展 | flit 化、hop、credit、VC、死锁及响应通道 |

Flat crossbar 必须明确一个 128 B beat 的四个 32 B fragment 是否能同周期发往四个输出。若每输入每周期只发一个 fragment，就已将每端口能力限制为 32 B/cycle；不能仍按 128 B/cycle 计算期望值。

每条链路描述 bytes/cycle、latency、II、buffer、方向；请求 header 也占命令容量。写数据和读返回的网络分别建模。多级链路的切片可采用 store-and-forward 作为初始明确策略；若支持 cut-through 必须另定义 credit 和包占用。

对照采用两组：①相同频率和明确链路预算的算法比较；②加入频率敏感性和资源代理约束的工程比较。输出交换端口×宽度、缓冲位数、pipeline 位数、跨组流量等代理指标，不能直接称其为真实面积/功耗。

## 8. 队列、仲裁及正确性

必备队列策略：每入口单 FIFO；按目标 Bank 或 group 的 VOQ；Bank 侧读写队列。分别固定队列项总数和存储总字节做公平对照，避免 VOQ 因总缓冲量无意增加而获益。每条队列必须说明以 transaction、beat 还是 fragment 计数。

仲裁包含 RR、加权 RR、年龄优先保护，以及可选读优先/写 drain。设定年龄阈值和最大连续读/写服务，记录最低端口进展。长 burst 默认按 beat/fragment 竞争；burst 锁定作为对照，不能隐式长期占用路由。

AXI 主要范围：INCR、完整/窄传输、WSTRB、多个 ID、不同 ID 并发、R/B 消费反压；FIXED/WRAP 可另实现，但支持矩阵必须明确，对不支持请求报错，不能静默当作 INCR。检查 burst 长度、size、地址合法性及 4 KiB 边界；非对齐访问必须正确切分或在 profile 中明确拒绝。

保持同一端口同 ID 的适用 AXI 返回顺序及每 burst 的 R beat 顺序；不同端口相同 ID 不得错误串行化。AXI4 W 没有 WID，按 AW 对应顺序关联 W；可缓存先到 W，但不能在未知 AW 时映射它。R/B 缓冲有限，回压必须传播到 Bank/fabric。

本模型选择保守写完成：全部有效 fragment 提交 SRAM 后才生成该 burst 的 B，且仍受 BREADY 限制。读在 Bank 服务定义的取样点取值，禁止从“仿真时刻最新”的全局数组随意读取。跨读写通道不凭相同 ID 推导全局顺序；闭环 workload 对依赖显式等待完成。跨端口无同步数据竞争不承诺确定结果；验证采用记录的 Bank 提交/取样顺序作合法性依据。

fragment 独立取得资源，完成后释放；不一边占住某些 Bank 一边等待其余 Bank。返回 buffer 可在派发前预留或使用严格 credit 机制，必须确保不存在循环等待。

## 9. ECC 必须进入性能路径

ECC 先支持 none、SECDED 64-bit data、SECDED 128-bit data。扩展 Hamming SECDED 对应常见存储为 72 bit、137 bit；仅是逻辑码字开销，实际宏宽度/填充另算。

参数区分编码延迟、解码延迟、II、实例数、共享位置、纠错额外周期。固定加一拍不足以表达共享 ECC 引擎饱和。无错误读也消耗解码资源。

完整覆盖一个 codeword 的写可直接生成校验；未覆盖完整 codeword 的 WSTRB/窄写默认 RMW：读取旧数据→ECC decode→按 mask 合并→encode→写回。锁定对应 codeword 防止丢失更新，并计入两次 Bank 服务、网络是否往返、锁等待及 ECC 资源。多个完整 codeword 写不能因未写满整 Bank word 就误判为 RMW。

scrub 使用真实低优先级带宽且有最大等待约束；可纠错错误的立即/延后修复策略参数化；不可纠错错误产生配置规定的 SLVERR/poison 及统计事件，不能悄悄提供“正确”数据。性能注错与真实 SECDED 数据校验分别验证；性能探索不需要门级 XOR 电路。

## 10. NPU workload：三类互补

### 10.1 合成微基准

顺序连续读/写；8 端口均匀无冲突；8 端口同 Bank 热点；多端口同相/错相流；2D stride；转置；随机/热点；1/4/8/16/32 beat burst（服从 4 KiB 限制）；不同 outstanding；读写比 100:0、75:25、50:50、0:100；不同 WSTRB 密度；返回端间歇阻塞；部分写+scrub。

### 10.2 算子访存生成器

GEMM：显式 M/N/K、dtype、tile shape、loop order、A/B/C layout、leading dimension、每 Tile 本地 buffer 容量及数据复用；不能假设每个 MAC 都访问共享 SRAM。

Attention prefill：Q/K/V tile、阶段依赖、片上重用、中间结果驻留；decode：KV 长步长、head 分布、低并行度和 append 小写。另覆盖 convolution/embedding、transpose/layout conversion、DMA 搬运与计算并发。模型只需生成访存和计算时间，不必实现完整数学算法。

### 10.3 Trace 与闭环

Trace 记录 op/address/size/burst/ID/mask/earliest_release/dependency/tile/tensor 标签，并记录生成来源。固定时间开环 trace 用于测服务曲线、饱和点和排队，报告 offered 与 admitted 流量差异；不能仅据此声称 NPU 加速。

闭环引擎表达 load tile → buffer ready → compute(C cycles) → store output；有限 ping-pong buffer、DMA outstanding、依赖和生产消费速率决定下一批请求。回包变慢会自然推迟计算/后续请求；同一个 workload 在不同架构上应重新闭环执行。输出 kernel makespan 和计算单元的 memory-stall 周期。

初始端口角色至少覆盖：8 个对称 Tile；4 个 Tile+2 个搬运源+2 个结果/辅助源；以及加权不均衡。端口角色是流量 profile，不应固化在 SRAM 架构。

## 11. 实验设计

E0 正确性与资源语义：固定一 Bank/一端口，验证数据、mask、RMW、latency/II、反压。

E1 固定 C 组织、flat fabric：扫描 modulo 的粒度、XOR、region；同时包含实际 layout 和对抗 stride。分清 mapping 与 layout 的独立效果。

E2 固定容量与理想聚合 Bank 带宽：比较 A/B/C/D；分别记录 fragment 数和路由代价。

E3 选取 E1/E2 有代表性的 3–5 个配置：flat、4-group、必要时多级，分别施加相同与受限跨组链路预算。

E4 对候选比较 FIFO/VOQ、outstanding 8/16/32/64、buffer 预算、RR/年龄保护、读写调度；检查 HOL 和端口饥饿。

E5 注入 ECC、partial write、scrub、返回背压，确定正常/退化边界；1R1W 与 1RW 单独对照。

E6 闭环 NPU 评估：使用训练外 shape/layout/seed 作保留集，给出各算子 makespan、最差退化和资源 Pareto。先筛选主效应，再对最终候选补做交织×路由×队列交互实验，避免单因素搜索遗漏最佳组合。

每次运行记录配置全文/hash、源码 revision、编译/依赖版本、workload hash、seed、warmup、测量区间、drain 规则。短算子直接测完整运行，不裁掉启动时间；稳态微基准区分 warmup 和 measurement。随机负载至少 5 个种子并给出置信区间，必要时增加样本，不能只选最好一次。

## 12. 指标合同

| 指标 | 定义/要求 |
|---|---|
| 有效吞吐 | 读消费或写提交的有效 payload 字节/测量时长；WSTRB 无效字节不计，读写分列 |
| 外部完成吞吐 | 写 B 消费、读最后 beat 消费视角，和后端提交吞吐分开 |
| 利用率 | Bank busy、链路 busy、ECC busy 分别统计；分母匹配单/双端口能力 |
| 流量放大 | SRAM 服务字节、内部网络字节/有效 payload 字节，分别展示 |
| 延迟 | release→accept、accept→首个 R、accept→末 R/B、release→完成，p50/p95/p99/max |
| 冲突 | 多个 ready 请求竞争同 Bank 的事件比例；和该请求因冲突等待的周期分开 |
| HOL | 队首阻塞而后续请求具备可服务目的地的周期 |
| 队列压力 | 平均/峰值占用、full cycles、ROB 阻塞、credit stall |
| NPU 结果 | makespan、compute active、memory stall、overlap、相对理想内存的 slowdown |
| 公平性 | 每端口吞吐、最长无进展区间；非对称 QoS 用归一化份额评价 |
| ECC | RMW 次数/放大、scrub 占用、纠错/不可纠错、lock wait |

延迟分解使用互斥状态周期或明确标记重叠因素，不能把并行 fragment 的等待简单相加当成请求延迟。至少保留可追踪的少量完整 transaction 时间线以解释瓶颈。

## 13. 代码交付结构与接口

建议新模型位于 esl-repo/models/memory/npu_sram_controller/，以下为本规划建议，不代表现有仓库已采用：

```
model.yaml                 # 模型身份、能力、参数、限制、依赖
configs/                   # baseline、拓扑、ECC、实验配置
include/                   # transaction、component contracts
src/frontend/              # AXI 语义与有限缓冲
src/mapping/               # 地址策略和独立逆映射
src/fabric/                # flat、hierarchical、接口抽象
src/memory/                # Bank、scheduler、ECC
src/workload/              # microbenchmark、NPU DAG、trace
src/observe/               # counters、events、report export
tests/                     # 单元、解析微基准、随机正确性
experiments/               # E0–E6、可复现脚本
docs/                      # 合同、模型限制、校准、实验结论
```

AddressMapper::map(addr, mask) 返回 fragment 列表并提供 inverse；Fabric::can_accept/submit 明确 backpressure；Bank::issue 返回资源预留/完成事件；Observer 通过事件订阅采样，不能改变调度结果。禁止 mapping、ECC 或 fabric 策略散落在一个巨大 controller 类中。

示意配置（实现时须形成 schema、默认值和范围校验；容量均为有效数据容量）：

```yaml
model: npu_sram_controller
mode: performance_checked
clock_hz: 1000000000
axi:
  ports: 8
  data_bits: 1024
  read_outstanding_per_port: 32
  write_outstanding_per_port: 32
memory:
  usable_capacity_bytes: 8388608
  physical_banks: 32
  bank_data_bits: 256
  port_type: 1RW
  read_latency_cycles: 2
  write_latency_cycles: 1
  initiation_interval_cycles: 1
mapping:
  policy: modulo
  stripe_bytes: 32
fabric:
  topology: flat
  fragment_bytes: 32
  fragments_per_input_per_cycle: 4
  output_bytes_per_bank_per_cycle: 32
  read_return_bytes_per_port_per_cycle: 128
  pipeline_cycles: 2
  request_and_response_resources: separate
queues:
  policy: per_bank_voq
  total_request_entries_per_port: 128
  request_entry_unit: fragment
  read_reassembly_beats_per_port: 128
  write_data_beats_per_port: 128
arbitration:
  policy: round_robin_age_guard
  max_wait_cycles: 256
ecc:
  mode: secded
  data_bits_per_codeword: 64
  scope: per_bank
  codewords_per_cycle_per_bank: 4
  decode_latency_cycles: 1
  encode_latency_cycles: 1
  partial_write: locked_rmw
  scrub: disabled
```

这里 ECC 的四个 64-bit codeword/cycle 与 256-bit Bank 吞吐匹配。其实现可以是并行 lane，模型必须计资源，不得把单个 64-bit/cycle 引擎写成能服务整个 Bank。

## 14. Agent 可执行工作包与验收

| 包 | 实现内容 | 前置 | 验收 |
|---|---|---|---|
| W0 | schema、事务合同、确定性内核、manifest | 无 | 非法配置拒绝；固定 seed 可复现 |
| W1 | 功能参考、modulo/XOR、split/reassemble | W0 | 全小容量地址双射、mask 无丢失；跨边界随机检查 |
| W2 | Bank latency/II、1RW/1R1W、仲裁 | W1 | 单 Bank 连续/冲突流与解析事件时刻一致 |
| W3 | 8 端口前端、ID、AW/W 解耦、ROB | W2 | 返回顺序、反压、drain 和 transaction 守恒 |
| W4 | flat/hierarchical、链路及 credit | W3 | 切窄链路产生相应瓶颈；请求/返回均受限 |
| W5 | ECC、partial-write RMW、scrub | W4 | 无丢失更新、资源放大正确、注错响应正确 |
| W6 | microbenchmark、GEMM/attention DAG、trace | W3 | 人工可算小 DAG 时刻精确匹配；内存变慢导致依赖后移 |
| W7 | E0–E6、报告、保留集、校准适配 | W4–W6 | 每条结论有 run id；报告全部候选及失败原因 |

持续不变量：accepted = completed + inflight（按事务/beat/fragment 分层核对）；容量不越界；无重复完成；有效字节总量守恒；Bank/link/ECC 每周期服务不超配；运行停止后可 drain；活跃无进展达到阈值时输出依赖和队列快照而非静默挂死。

必须有的负例：同 Bank 热点；XOR 对某些 stride 仍冲突；返回链路过窄；单 FIFO 的 HOL；ECC 引擎过窄；部分写 RMW 争用；高优先级持续流导致低优先级等待；4-group 远端访问饱和。

解析确定性微基准要求完成时刻与预期一致；随机正确性不接受数据错误。与未来 RTL 的吞吐偏差 ≤5%、均值延迟偏差 ≤10% 可作为校准目标，但必须声明适用负载、计时起止点和误差来源；校准前不得宣称达标。p99 需足够样本并单列，不以平均值替代。

## 15. 建议执行优先级与边界

先完成 C 基线 + flat + modulo/XOR + 微基准，证明模型会产生真实冲突和回压；紧接着补 hierarchical 与闭环 GEMM/attention，再将 ECC/RMW 纳入候选筛选。先后顺序是研发依赖，不削减最终交付范围。

本次必须规划并实现的核心是 SRAM 共享存储与 NPU 性能关系。CPU 指令仿真、完整 NPU 数值模型、门级 ECC、完整 DFT/MBIST、完整 APB 寄存器表均非探索所必需；MBIST/低功耗等若影响运行期可用服务能力，可通过 Bank offline/wakeup 事件扩展。多时钟配置只有在实现异步队列/同步可见延迟后才能宣称支持，初始统一时钟。

建议第一轮重点回答三个问题：①32×256-bit、细粒度交织是否比 8×1024-bit 在目标 NPU layout 下更稳健？②可逆 XOR 对热点的改善是否抵消路由与局部性损失？③4-group 路由在编译器 Bank 着色下能否接近 flat 的 makespan，同时减少全局交换资源？这些均为待验证假设。

## 16. 公开资料及使用边界

- [PULP AXI](https://github.com/pulp-platform/axi)：可参考 burst splitting、宽度转换、crossbar、ID/并发和模块组合；其 interleaved crossbar 标记 experimental，不能将其当作已验收产品直接背书。本规划内部 fragment 网络不要求每跳仍使用完整 AXI。
- [NVIDIA CUDA Best Practices：Shared Memory](https://docs.nvidia.com/cuda/archive/10.2/cuda-c-best-practices-guide/index.html)：支持将 stride、Bank 冲突和 padding 纳入实验的依据。GPU warp/shared-memory 的具体广播规则和 Bank 数不可直接移植成本控制器规则。
- [Accellera SystemC reference implementation](https://github.com/accellera-official/systemc)：仿真基础设施来源。SystemC 本身不会自动提供本文的 AXI 语义、Bank 仲裁或 NPU 性能精度。

上述具体架构、参数及实验合同为本项目工程建议，不是对公开实现性能的复述。
