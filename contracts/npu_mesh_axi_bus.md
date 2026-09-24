# 文王 NPU 总线 IP Suite：需求与实现契约

版本：v0.1（提议稿）  
调研日期：2026-09-24  
适用对象：架构 Agent、IP 开发 Agent、验证 Agent、ESL 与物理实现团队。  
目标：构建适合端侧约 4B 多模态模型的数据互连，支持高吞吐 Prefill、低尾延迟 Decode、KV 搬运、多 Tile 数据复用和可控的共享存储访问。

## 0. 契约状态、优先级与适用范围

本文件同时给出功能需求契约与候选实现约束，二者必须分开使用。本文所有“必须”表示拟纳入文王产品的要求，不表示现有 RTL 已经实现或验收。竞争力目标为工程目标，不是与商业 IP 的实测胜负结论。

遵循既有研发流程：**Python 行为模型 → OR-DR 冻结 → 资源受限 ESL → 架构冻结 → LLD/RTL → 证据门禁。** 在 OR-DR 阶段不得把本文候选 Tile 数、链路位宽、VC 数、描述符位宽、FIFO 深度直接固化。此时先确定功能、责任、错误语义及可观测行为；ESL 后才将选定 profile 的编码、端口、深度冻结为实现 ABI。

| 标记 | 含义 | 发布要求 |
|---|---|---|
| P0-F | 首版必须的功能、正确性或隔离要求 | 不允许静默省略 |
| P0-C | 首版竞争力要求 | 允许换实现方式，但必须给出指标证据 |
| P1 | 增强功能 | 关闭时必须有明确能力位和行为 |
| P2 | 后续探索 | 不得成为 P0 的隐性依赖 |
| A | 候选架构/参数 | ESL 与物理实现比较后冻结 |

范围包括：Data NoC、边缘 NIU、命令/事件网络、同步单元、链路桥、管理与观测，以及与这些 IP 强耦合的 Tensor DMA 契约。BMU 分配策略、DDR PHY/控制器、SRAM bank 内部仲裁、Tile 算子实现、CPU cache coherence 均由独立 IP 负责。不能借“总线能力完备”把它们重复塞进 Router。

## 1. 业界调研与文王采用原则

以下事实基于公开的一手文档。商业产品的可选功能、内部微架构和实际 PPA 不一定公开；不得从产品宣传推导未经披露的 VC 数、缓冲深度或带宽保证。

| 参考对象 | 已公开能力 | 对文王的设计启示 | 不直接照搬的内容 |
|---|---|---|---|
| Arm CoreLink NI-700 [S1] | 包化互连、注入端 QoS 调节、事务跟踪/重排、按 ID 约束目标来规避事务依赖 | 顺序、目标绑定、重排资源应成为 NIU 显式契约；QoS 要覆盖注入点 | 不把 AXI 的五个通道逐根穿过整个 Mesh |
| Arteris FlexNoC 5 [S2] | 带宽与时延调节、广播/组播、时钟/电源域、物理感知和可选安全功能 | NoC 交付物需要拓扑、布线、时序、功耗与运行时观测证据 | 不声称自行实现这些特性便等价于商业 IP 或获得 ASIL 认证 |
| ETH/UniBo FlooNoC [S3][S4] | 面向非一致性系统；协议处理集中在 NI；多物理通道和宽窄链路分工 | 单独比较“宽数据＋窄请求/响应”与共享物理链路方案；简化 Router | 不将其特定工艺的论文能效数字当作文王指标；不假定它以 credit 为唯一流控 |
| Berkeley Constellation [S5] | 生成式 wormhole 网络、virtual networks、credit 流控和路由检查器 | 将路由合法性、依赖分析、生成配置检查做成工具交付物 | 不因参考其架构而强制引入 Chisel 技术栈 |
| Tenstorrent TT-Metalium [S6][S7] | 面向核网格的异步数据搬运、矩形组播及相关 barrier API；矩阵乘例程使用数据广播 | 显式搬运、接收准备和完成语义应服务软件；组播必须接入真实工作负载 | 其芯片的接口、地址编码及拓扑不等同文王 |
| Arm AMBA AXI [S8] | 定义外部事务与顺序模型 | 边缘适配以官方规范为准，自定义 NoC 不得破坏外部协议 | 不把 AXI4 与 AXI5 原子、一致性或扩展属性混为一谈 |

**文王建议：非一致性 Data Mesh＋独立 command/event 通路＋独立管理接入＋AXI 边缘适配。** 全局 cache coherence 不进入 P0；CPU/NPU 共享内存通过 ownership、cache maintenance 和同步契约管理。可选 I/O coherent 系统端口由独立桥接 IP 提供。

竞争力由五项结果证明：有效数据带宽、Decode 尾延迟、数据复用减少的链路字节数、每字节能耗/布线代价、软硬件可调优性。链路位宽与 Router 个数本身不是性能结果。

## 2. 系统分工和 IP 产品边界

### 2.1 三类通路

| 通路 | 搬运内容 | 建议结构 | 强制边界 |
|---|---|---|---|
| Data | Weight/Activation/KV/State/Intermediate，读写请求与响应 | 参数化非一致性 NoC | 路由、流控、事务终结、数据保护 |
| Command/Event | Scheduler 宏任务、DMA 命令、完成、异常、barrier | 每 Tile 独立队列/直连；返回可分级汇聚 | 数据拥塞不得占用其队列；事件不会比数据可见性更早 |
| Management | CSR、拓扑发现、计数器、错误、低功耗配置 | AXI-Lite/APB 管理网络 | 数据面阻塞时仍能诊断和发起恢复 |

物理独立 command/event 不代表必然能执行新任务：Tile、DMA 或数据目标本身不可用时，仍须显式 backpressure。独立性保证的是网络资源不被大数据包耗尽。

### 2.2 对外发布的 IP

| IP / 建议 VLNV 名称 | 主要职责 | 不承担的职责 | 优先级 |
|---|---|---|---|
| `aixsilicon:ip:noc_router` | 单 Router 路由、VC/物理通道、仲裁、credit、本地统计 | AXI ID、地址翻译、张量格式、BMU 分配 | P0-F |
| `aixsilicon:ip:noc_axi_niu` | AXI requester/subordinate 两种角色的包化、事务表、重排、错误映射 | 算子调度、NoC 拓扑决策 | P0-F |
| `aixsilicon:ip:noc_tile_niu` | Tile 数据收发、Local SRAM 可见性、buffer token 校验、接收资源 | Tile 内算子执行、全局 buffer 分配 | P0-F |
| `aixsilicon:ip:noc_memory_niu` | 全局地址到 memory endpoint、AXI 接出、memory QoS/错误回传 | SRAM bank ECC/仲裁或 DDR 时序 | P0-F |
| `aixsilicon:ip:tensor_dma` | 多维/SG 搬运、描述符分段、组播源端展开、完成聚合 | KV 分配/驱逐决策、Router 路由 | P0-F/P0-C |
| `aixsilicon:ip:command_event_fabric` | 独立命令发射、完成/错误回传、上下文隔离 | 运行 DAG 的完整 scheduler | P0-F |
| `aixsilicon:ip:sync_event_unit` | 事件计数、依赖到达、barrier epoch、取消 | 内存一致性和算子计算 | P0-F |
| `aixsilicon:ip:noc_link_bridge` | pipeline、CDC、width conversion、链路启动与隔离 | 透明地掩盖丢包或故障 | P0-F |
| `aixsilicon:ip:noc_management` | CSR/PMU、配置提交、fault 聚合、quiesce 管理 | 自动决定全系统 DVFS 策略 | P0-F/P0-C |
| `aixsilicon:ip:noc_stream_niu` | 有界 AXI-Stream/专用流端点适配 | 完整张量调度 | P1 |
| `aixsilicon:ip:noc_collective_engine` | 集中或分布式 gather/reduce 等集合操作 | 在 Router 中隐式执行算术 | P1/P2 |

`noc_axi_niu` 为可配置共用核心，Tile/Memory NIU 是带端点策略的 wrapper，不能独立再实现一套不兼容的 AXI tracker。发布的 System Generator 负责装配以上 IP；不把生成器本身假装成一个有 CSR 的硬件模块。

### 2.3 建议 CBB

| CBB | 用于哪些 IP | 可独立验收的行为 |
|---|---|---|
| `noc_route_xy` | Router | 对合法坐标输出单调 XY 下一跳、local 命中、非法目的检测 |
| `credit_counter` / `credit_link_ctrl` | Router/Bridge | credit 守恒、初始化、溢出/下溢检测 |
| `vc_fifo` / `fifo_sync` / `fifo_async` | 多 IP | 容量、顺序、CDC、满空、可选 ECC |
| `vc_allocator` / `switch_allocator` | Router | 唯一授予、无资源误分配、公平前进 |
| `weighted_arbiter` / `age_tracker` | Router/NIU | 权重、老化、饥饿界限的局部条件 |
| `packetizer` / `depacketizer` | NIU | 包长、head/tail、掩码、分片重组 |
| `tag_pool` / `txn_tracker` | NIU/DMA | 唯一分配、epoch、在途生命周期 |
| `reorder_buffer` | NIU | 同 ordering domain 退休、容量预留 |
| `addr_decode` / `range_checker` | NIU/DMA | 范围、溢出、属性、跨边界检查 |
| `width_adapter` / `byte_lane_mapper` | Bridge/NIU | 字节精确映射、WSTRB、尾部有效字节 |
| `token_bucket` / `rate_meter` | NIU | 注入整形和计费，burst 上界 |
| `multicast_pending_mask` | DMA/可选 Router | 每个接收者恰好一次、慢分支跟踪 |
| `event_counter` / `barrier_scoreboard` | Sync | 去重、epoch、防早完成 |
| `crc` / `parity` / `ecc` | Bridge/FIFO | 检错覆盖与故障注入；不宣称认证 |
| `perf_counter` / `histogram` / `fault_fifo` | 多 IP | 快照、溢出、无调试反压 |

CBB 不携带完整 AXI/APB 寄存器外壳。APB/AXI front-end、防火墙 wrapper、DMA descriptor fetch 仍属于 IP。

## 3. 共用事务和完成契约

### 3.1 需求

| ID | 等级 | 必须满足的要求 | 验收方式 |
|---|---|---|---|
| SYS-001 | P0-F | 任一被接收的合法事务最终得到一个成功、失败或明确 abort 终态；正常运行不丢、不重、不串上下文 | Scoreboard＋形式安全性质；活性在公平服务假设下验证 |
| SYS-002 | P0-F | 事务唯一键至少语义包含 source、context、epoch、tag；tag 复用前旧响应不可误匹配 | tag 回绕/迟到响应/复位注入 |
| SYS-003 | P0-F | 地址、长度相加发生溢出必须拒绝；零长度 DMA 为定义明确的 no-op，不生成 AXI 零 beat | 边界值测试 |
| SYS-004 | P0-F | Read/Write/Copy/Multicast/Fence 的接收、发出、目标可见、任务完成是不同状态 | 时序扰动下检查事件 |
| SYS-005 | P0-F | 不承诺多个 Tile 的任意读写全局有序；定义 flow/order-domain 内顺序及显式 fence | 双 producer/consumer litmus |
| SYS-006 | P0-F | CPU cache maintenance/ownership 由 Runtime 完成；NoC 不将普通 ACK 标为 cache coherence 完成 | 集成指南与共享 buffer 测试 |
| SYS-007 | P0-F | 拒绝请求不产生目标写副作用；运行中故障可产生 partial effect，必须报告 | 跨页/分片故障测试 |
| SYS-008 | P0-F | 错误路径和管理路径不依赖故障数据队列才能报告 | 饱和＋fault 测试 |
| SYS-009 | P0-C | 允许多 context、多 flow、多个未完成事务；容量可配置并且资源不足可观察 | outstanding sweep |
| SYS-010 | P0-F | 所有跨 IP 的 completion/abort、权限属性、地址空间、epoch 由共用 schema 定义 | schema/codegen 一致性 |

### 3.2 完成层级（禁止混用）

| 状态 | 含义 | 能否释放源/启动消费者 |
|---|---|---|
| ACCEPTED | 请求已进入本地队列，句柄有效 | 否 |
| SOURCE_RELEASED | 源数据已全部读出并由系统内部可靠持有，后续不会再读源 | 可按契约释放源；不代表目的有效 |
| INJECTED | 最后一个包进入网络 | 不能代表目的完成 |
| TRANSPORT_ACK | 端点已完整接收包 | 除非 endpoint 明确将其定义为写可见，否则不能 |
| TARGET_VISIBLE | 数据已在指定观察域中可被后续消费者读到 | 可发布该域的数据就绪事件 |
| TASK_DONE | 所有必需分片/目的端达到目标完成级别，且状态聚合完毕 | 可按任务契约推进依赖 |

P0 默认只向 Scheduler 发布 `TASK_DONE`，其前提为 `TARGET_VISIBLE`。可选 source-release 事件必须独立编码。AXI B 响应是外部协议的写响应，不能普遍推导为任意 CPU/Tile 已可见；Memory NIU 必须根据所接控制器及可见域定义 commit/fence，必要时利用其 drain/顺序保证。DDR 写完成不要求数据已物理落入 DRAM cell，但要求目标观察域中读后写可见条件成立。

### 3.3 fence 与错误

FENCE(scope, context, order_domain, watermark) 捕获该 domain 已接收事务的水位；只有不晚于水位的相关事务均 TARGET_VISIBLE 或被记录为失败，才返回。水位之前任何事务失败，fence 返回失败而非成功。它不等待之后提交的无关事务。

内部错误类建议：`DECODE`、`DENIED`、`UNSUPPORTED`、`TARGET`、`DATA_CORRUPT`、`TIMEOUT_UNCERTAIN`、`ABORTED`、`STALE_EPOCH`、`PROTOCOL`。失败描述至少含 context、tag、地址/偏移、目的、错误类、已知完成范围与是否可能有未知副作用。多目的返回 completed/failed/unknown 位图；大集合允许索引式结果表。

## 4. 内部 NoC 协议契约

### 4.1 语义字段

以下是字段的语义集合，**不是强制一拍容纳的物理头格式**。可以按 opcode 编码成不同 head/extension，架构冻结时由 schema 输出精确位段、长度单位、端序及保留位。

| 字段组 | 内容 | 规则 |
|---|---|---|
| 版本/类型 | protocol_version、opcode、flags | 不支持版本/opcode 可诊断地拒绝 |
| 路由 | src_endpoint、dst_endpoint，必要时 dst_x/y | 逻辑 endpoint 与物理坐标分开；generation map 唯一 |
| 身份 | context_id、epoch、txn_tag、flow_id | 不允许用户可写 payload 伪造可信安全身份 |
| 顺序 | order_domain、sequence、水位 | 同 ID 顺序在 NIU 终结；不依赖网络全局顺序 |
| 地址 | physical_addr 或 local_buffer_token＋offset | 两种寻址显式区分；Router 不翻译 token |
| 分片 | total_len、fragment_offset、fragment_len、last_fragment | 单位均为 byte；范围不得重叠/越界 |
| 策略 | VN、QoS class、security attr、可选 deadline hint | VN 与优先级分离；软件无权随意选择保留 VN |
| 数据 | payload、byte_enable/valid_bytes、poison | 有效掩码覆盖稀疏写；不得将未使能字节覆盖为 0 |
| 集合 | multicast group、group_version、destination subset | P0 允许源 NIU 展开；Router 不必理解组 ID |
| 返回 | response_status、completion_level | 明确返回数据是否可信 |

### 4.2 包与链路需求

| ID | 等级 | 要求 | 验收 |
|---|---|---|---|
| NET-001 | P0-F | 包有明确 head/tail、长度、类型；单 flit 包可 head+tail 同时成立 | 随机长度 round-trip |
| NET-002 | P0-F | 每输入 VC 一次只承载一个未结束包的连续语义；允许不同 VC 的 flit 在物理链路交错 | 交错/反压 scoreboard |
| NET-003 | P0-F | Router 不等待整个 AXI burst 才释放网络；包长上界独立于原 AXI burst 长度 | 超长 burst 竞争短包 |
| NET-004 | P0-F | 所有会竞争共享输出的包必须具备有界 max_packet_flits；大事务在 NIU 分段 | 最大包边界和公平性 |
| NET-005 | P0-F | 编码包含完整 byte-enable 或等价无损表示；头开销和 mask 开销进入性能计算 | 非连续 strobe |
| NET-006 | P0-F | 非法 head、提前/缺失 tail、长度异常进入隔离/恢复；不能永久保留 VC | 协议负向注入 |
| NET-007 | P0-F | credit link 每次发送消耗目标 VC 一个接收槽；归还只发生在该槽真正释放时 | 守恒断言 |
| NET-008 | P0-F | credit 的反向路径不竞争需要它解锁的正向数据缓冲 | 依赖图检查 |
| NET-009 | P0-F | link-up 前不得发送；双端确认 epoch/深度后建立初始 credit；复位不得重复发放 | 单边复位/不同启动次序 |
| NET-010 | P0-F | 流控信号、数据和 VC ID 的 pipeline 延迟配套；禁止多 Router 组合 ready 长链 | lint/STA＋延迟插入 |
| NET-011 | P0-C | 插入 pipeline 后无竞争稳态仍可达到每条物理通道每周期一 flit | 长流量 microbenchmark |
| NET-012 | P0-F | 软件-visible 能力、仿真模型和 RTL 使用同一 profile/hash | 集成时检查 |

候选 P0：wormhole＋credit、确定性 XY；不是宣称所有业界 NoC 都采用此组合。链路数据、路由 sideband、credit 和保护 bit 的物理开销必须分项报告。

## 5. 路由、VN/VC 与死锁契约

### 5.1 功能与候选资源划分

VN 是避免协议依赖循环的资源类别；VC 是物理通道上的缓冲/仲裁实例；QoS 是服务策略。三者不能互相替代。

| 逻辑类 | 承载内容 | 候选隔离方式 |
|---|---|---|
| REQ | 读请求和其他不带大 payload 的请求 | 专用 VN；可选独立窄物理链路 |
| WRITE | 带数据的写/推送分片 | 专用 VN，NIU 先准备好完整有限分片 |
| RDATA | 读返回 | 专用 VN，与 WRITE 的接收缓冲隔离 |
| RESP | B/ACK/错误终结 | 专用 VN；可选独立窄物理链路 |

上述四类是依赖审查的保守起点，**不等于已证明“四个 VC 足够”**。同一 VN 可因 QoS/HoL 再配置多个 VC。合并任意类别需要重新证明 endpoint 依赖与缓冲预留安全。Command/Event 不放入这些 VN。

### 5.2 可实现的 P0 保守方案

1. AXI 写：source NIU 按 AW 顺序关联 W，先收齐一个有界内部片段，再向 WRITE 注入。包已进入网络后不会因等待未到的外部 W 而永久占有路由通道。
2. 读请求注入前，source 为该请求的返回字节和元数据预留接收资源。基线按整个外部 burst 或其已准许子请求预留，不能在发出后再赌 ROB 有空位。
3. 写片段发出前为终结 ACK 预留槽。memory endpoint 收到包后可在独立队列等待目标；响应能够脱离请求通路。
4. 同源同 AXI ID 对跨目标访问采用 P0 保守的“在途期间单目标绑定”，读写独立管理。源端可先将跨目标 burst 分解为串行受控子事务；P1 使用有容量保证的 ROB 并发优化。
5. target NIU 用完整内部 transaction key 关联分片；向 AXI target 发 AW/W 时保证目标端 W 与 AW 次序一致。不能假定穿过网络的多个源仍天然保持此关系。
6. Tile push 在发出前持有 destination buffer reservation；目标可接收已准许的数据，即使 compute 暂停。不得要求消费者先从同一个阻塞的 WRITE VC 发出请求才释放此 reservation。
7. 对等待目标服务的队列、返回队列、局部重排和 multicast 依赖一起构图；正常前进性依赖目标最终服务、消费者最终释放和仲裁公平。

这个方案会增加 NIU 缓冲。ESL 必须同时测量内存容量、在途度及性能，后续优化不能删除预留而保留原活性结论。

| ID | 等级 | 要求 | 验收 |
|---|---|---|---|
| DDL-001 | P0-F | 生成完整 channel/resource dependency graph，覆盖 Router、NIU、目标、重排、返回、组播 | 图与证明摘要入 release |
| DDL-002 | P0-F | 基线 XY 无绕回；网格边缘及 local 路由唯一；不混用无约束 XY/YX | 穷举 src/dst、turn 检查 |
| DDL-003 | P0-F | 必须证明路由级无环与事务级前进性；不能仅写“XY 无死锁” | formal＋构造循环压力 |
| DDL-004 | P0-F | 不把 timeout 当作正常无死锁证明；timeout 仅处理异常 | 人为循环测试必须暴露故障 |
| DDL-005 | P0-F | 返回缓冲预留、保留 VN、出口仲裁必须覆盖所有合法 opcode | 配置检查＋容量耗尽测试 |
| DDL-006 | P0-C | HoL 阻塞要可测；独立 flow/目的地分队列或 VC 方案进入比较 | 单热点＋冷流对照 |
| DDL-007 | P1 | 自适应路由要求 escape 资源、受限转换规则和独立活性证据 | 未证明则配置生成失败 |
| DDL-008 | P1 | 故障绕行不得临时打破路由依赖；需新 route epoch、drain 或受控 abort | 链路故障恢复 |

## 6. noc_router contract

### 6.1 候选接口与内部责任块

接口：N/S/E/W/Local 的有向收发 link；边缘端口可裁剪；local 可经独立 mux 接多个 endpoint。配置与 PMU 可通过 management agent 间接访问。Router 不需要直接暴露完整 AXI slave。

责任块：Input VC Buffers → Route Compute → VC Allocate → Switch Allocate → Crossbar → Output Pipeline；credit return、PMU、fault checker 并行。VA/SA 合并、look-ahead、bypass 均需在时序和等价性证据之后启用。

| ID | 等级 | 要求 | 验收 |
|---|---|---|---|
| RTR-001 | P0-F | 端口数/坐标/拓扑由生成器约束，未连接端口不误发 credit | 1×1、1×N、2×4、边角实例 |
| RTR-002 | P0-F | 同拍同输出同物理通道最多一个获准发送者；input 不被非法双消费 | SVA |
| RTR-003 | P0-F | downstream VC 分配与 packet 生命周期一致，tail 后正确释放 | 单 flit/满包/反压 |
| RTR-004 | P0-F | 不在长包被外部暂停时垄断整个物理端口；其他有 credit VC 应可前进 | VC 交错测试 |
| RTR-005 | P0-C | 每输出独立仲裁，多组不冲突端口可同拍并行 | permutation 流量 |
| RTR-006 | P0-C | 权重可编程、等权公平；高优先级流量必须受 admission/整形约束 | 服务份额与饥饿测试 |
| RTR-007 | P0-F | 输入满、credit 空、route blocked 等 stall 原因分别计数 | 计数器对照 |
| RTR-008 | P0-F | Router 不修改 payload、身份及事务语义；只修改允许的 hop/link 元数据 | 端到端 hash |
| RTR-009 | P0-F | 未知/非法目的在注入端优先拒绝；中途发现损坏包须隔离并经管理报告 | 路由头 bit flip |
| RTR-010 | P0-C | Router pipeline 可配置，频率/面积/延迟结果与 profile 绑定 | 统一约束 PPA sweep |
| RTR-011 | P1 | multicast 复制逐分支跟踪，单分支停顿不允许向已完成分支重发 | 慢分支测试 |
| RTR-012 | P1 | bypass 必须遵守所有 credit、安全与仲裁性质 | 开关 bypass 等价比较 |

## 7. noc_axi_niu contract

### 7.1 能力 profile

接口角色用 `S_AXI`（接收外部 initiator）与 `M_AXI`（访问外部 target）表达，不能混淆 Router local 方向。参考规范为 AMBA AXI4 对应章节 [S8]；以下为文王产品策略。

| 能力 | P0 | 可选/约束 |
|---|---|---|
| 协议 | AXI4 五通道、AXI-Lite 管理 wrapper | AXI3、AXI5、ACE/CHI 不属于默认 profile |
| Burst | 合法 INCR 必须；FIXED/WRAP 可配置支持 | 不支持的合法类型明确返回错误；不能默默按 INCR 执行 |
| 尺寸 | full-width、narrow、合法 unaligned、WSTRB | 不支持组合由能力表限定且可诊断 |
| Length | 所有该 profile 合法 burst 长度；内部可分片 | 分片不改变源端 beat 数与 LAST |
| Outstanding | read/write 分开配置 | P0 候选每类 8/16/32/64；不是冻结值 |
| ID | 参数化、多 ID 并发、同 ID 顺序 | ID remap 不得碰撞；原 ID 必须正确恢复 |
| Exclusive | P0 默认关闭 | 如开启，需目标配合、monitor 生命周期及真实 EXOKAY 语义 |
| QoS/Protection | 明确映射 AxQOS/AxPROT；不丢安全属性 | AxCACHE/AxREGION/USER 按 schema 定义 |
| Atomic/coherent | 不支持 | 禁止把私有原子扩展伪称 AXI4 原子支持 |

### 7.2 必须实现的要求

| ID | 等级 | 要求 | 验收 |
|---|---|---|---|
| AXI-001 | P0-F | 对 source 所见 ready/valid 正确；已拉高 VALID 的 payload 在未握手时稳定 | VIP＋SVA |
| AXI-002 | P0-F | AW/W 独立到达；允许 W 先 valid，NIU 可 backpressure；不能假设同周期到达 | AW/W 极端错开 |
| AXI-003 | P0-F | AXI4 W 没有 WID；W 按 AW 接收顺序关联，跨源 target 仲裁不交错写 burst | 多源并发写 |
| AXI-004 | P0-F | source B 不早于 AW 和全部 W 接收及下游必要响应；每 burst 仅一个 B | 提前 B mutation |
| AXI-005 | P0-F | 同源同 ID 的 read response 顺序和 write response 顺序分别保持 | 同 ID 多目标/不同延迟 |
| AXI-006 | P0-F | 不提供隐含的同 ID read/write 互相排序；软件需等待/使用显式同步 | read-after-write litmus |
| AXI-007 | P0-F | R beat 数、RID、RLAST 与源请求匹配；错误 R 也要终结既定 burst | 中段失败、尾拍失败 |
| AXI-008 | P0-F | 分片、合并、width conversion 后字节地址和掩码保持，不能更改 Device 区访问次数 | byte scoreboard＋MMIO side effect |
| AXI-009 | P0-F | 对 non-modifiable/Device 访问遵守 AXI 属性约束；内部包分片不授权任意改变外部事务 | 属性矩阵 |
| AXI-010 | P0-F | 未映射地址用 DECERR；目标故障/不支持的目标操作按 documented policy 返回 SLVERR；详细错误留日志 | 错误映射表 |
| AXI-011 | P0-F | 拒绝的合法写仍排空其必要 W 后返 B；拒绝的读返回规定拍数；非法协议输入可隔离 | decode miss＋长 burst |
| AXI-012 | P0-F | 接入 master 必须遵守 4KB 等 AXI 边界规则；违法输入不得被描述为可无条件恢复 | VIP 负向检测 |
| AXI-013 | P0-F | source remap、destination、sequence、epoch 在 txn 表中保持到最终退休 | tag pool 满/回绕 |
| AXI-014 | P0-C | 已被一个 target 阻塞的事务不应无条件阻塞所有不同 ID/target 流 | 冷热目标测试 |
| AXI-015 | P0-F | downstream 故障超时后，如返回合成错误，必须 quarantine 对应 txn/ID 并丢弃迟到响应；不得误投给新事务 | timeout 后迟到 B/R |
| AXI-016 | P0-F | 对超时写不声称“没有发生写”；返回 uncertain effect，等待 drain/reset 后再复用受影响资源 | 延迟写副作用 |
| AXI-017 | P0-F | 部分 read 数据已经返回时，后续终结不可再次发送这些 beat；未返回 beat 用错误结束 | 中途复位/timeout |
| AXI-018 | P0-F | unsupported exclusive 不返回 EXOKAY；禁用 profile 采用拒绝并记录，避免误写成普通写 | exclusive 开关测试 |
| AXI-019 | P0-F | USER 字段按角色定义传播/解释/保留规则，不默认全零丢弃 | 自定义安全 USER |
| AXI-020 | P1 | 多目标同 ID 并发由有容量保证的 ROB 实现；保持外部顺序且不引入资源环 | ROB 满＋逆序响应 |

AXI 错误处理仅承诺对协议合法的请求进行定义明确的终结。若外部 master 永远不提供剩余 W 或永远不接收 R/B，互连无法承诺该 master 正常完成，必须将环境假设和隔离策略写入集成指南。

## 8. noc_tile_niu / noc_memory_niu contract

### 8.1 Tile 数据入口

| ID | 等级 | 要求 | 验收 |
|---|---|---|---|
| TNI-001 | P0-F | 显式支持 push 到 local buffer 和 pull/read 返回；支持任一模式需能力位 | 功能用例 |
| TNI-002 | P0-F | buffer token 包含所属 context/代次/范围/权限语义，offset+len 范围校验 | stale token/越界 |
| TNI-003 | P0-F | 接收 admission 基于已经预留的可写空间；不能只因 Router 有 credit 就认为 SRAM 空间充足 | compute 暂停＋持续输入 |
| TNI-004 | P0-F | local write commit 后才发 visible ack，commit 由 SRAM 接口合同定义 | SRAM 延迟/反压 |
| TNI-005 | P0-F | source-release 只在最后一次读取源 buffer 后发生 | double buffer 复用测试 |
| TNI-006 | P0-C | 支持 compute/load/store overlap，队列与资源冲突可统计 | overlap 基准 |
| TNI-007 | P0-F | Local SRAM 与 NoC 同时访问的仲裁由 Tile 内资源控制提供；NIU 不假设永远一拍 | bank contention |
| TNI-008 | P0-F | incoming 数据发生失败，buffer 标为不完整/poisoned，禁止发送成功 data-ready | 错误分片 |

### 8.2 Memory 数据入口

| ID | 等级 | 要求 | 验收 |
|---|---|---|---|
| MNI-001 | P0-F | global region→memory endpoint 映射唯一；权限/属性由配置定义 | 地址穷举/边界 |
| MNI-002 | P0-F | 支持多个 memory NI；单一跨 Mesh 出口不能成为未经测量的默认架构 | 多 NI 对比 |
| MNI-003 | P0-F | memory endpoint 内 bank 选择归 SRAM/DDR 控制器；NoC region interleave 与 bank interleave 分别定义 | 地址映射交叉检查 |
| MNI-004 | P0-F | request 拆分不可跨越不同权限/Device 属性边界；跨 region 的可修改内存事务按已定义子事务聚合 | 边界＋部分失败 |
| MNI-005 | P0-C | outstanding、burst、返回通道和 memory controller 队列能力匹配 | memory 延迟扫描 |
| MNI-006 | P0-F | NoC fence 的 target-visible 条件在每类 SRAM/DDR endpoint 有独立说明和测试 | read-after-fence |
| MNI-007 | P0-C | 传递 QoS/可信 context 至可支持的 memory 调度器；不支持时声明隔离上限 | 端到端 QoS |
| MNI-008 | P0-F | 动态地址映射更新必须 drain 并原子提交版本；禁止在途包一半用旧图、一半用新图 | 配置更新竞态 |

P0 不在 Router 内做虚拟地址翻译。IOMMU/页表解析可由系统端或 DMA 前级提供，必须在注入时形成受权限校验的物理地址。BMU 管理资源归属及迁移；NoC/DMA 消费映射或 token，不能自行改变 buffer placement。

## 9. Tensor DMA、组播和集合操作 contract

### 9.1 DMA 语义结构

行为模型采用可变长度语义结构，架构冻结后再编码为固定头＋扩展描述符：

```yaml
dma_task:
  version: semantic_v1
  identity: {context: symbolic, epoch: symbolic, task_id: symbolic}
  operation: COPY       # COPY / FILL / MULTICAST / GATHER
  source: {space: buffer_token, handle: symbolic, offset_bytes: 0}
  destination: {space: physical, address: symbolic}
  shape: {element_bytes: symbolic, counts: symbolic, strides_bytes: symbolic}
  segments: optional_list
  multicast: {group_id: optional, group_version: optional}
  policy: {qos: symbolic, order_domain: symbolic, overlap: forbidden}
  completion: {level: TARGET_VISIBLE, event_id: symbolic}
```

API 的 size/stride/offset 均有明确 byte 单位；负 stride 如不支持必须拒绝。P0 对源/目的重叠默认拒绝，不保证 memcpy 意义之外的 memmove 行为。FILL/GATHER 如在首版关闭，能力发现必须体现。

| ID | 等级 | 要求 | 验收 |
|---|---|---|---|
| DMA-001 | P0-F | 支持 1D/2D strided copy、SG/分段列表；多维通过描述符展开可扩展 | 多维 golden model |
| DMA-002 | P0-C | 支持多队列、多个 context、多 outstanding；描述符预取不越界 | descriptor/数据并发 |
| DMA-003 | P0-F | 大搬运按 4KB、最大 AXI burst、endpoint region 和 max_packet 边界合理分段 | 边界乘积用例 |
| DMA-004 | P0-F | descriptor 取得并验证后冻结本次执行快照，避免软件修改导致 TOCTOU；记录版本 | 并发改描述符 |
| DMA-005 | P0-F | Paged KV 以已解析的 segment list 或受控查表接口表达；DMA 不决定页分配与驱逐 | 非连续页 KV 搬运 |
| DMA-006 | P0-C | prefetch 与 demand 流分别限流，低优先级预取不得无界挤占 Decode | decode＋prefetch |
| DMA-007 | P0-F | 异步完成计数覆盖所有读写子事务；失败不能提前释放仍在访问的 buffer | 中途 fault/abort |
| DMA-008 | P0-F | abort 先停止新子事务，再 drain/隔离在途；报告 partial/unknown effect | 多子事务取消 |
| DMA-009 | P0-C | 相邻小段可合并，但不能跨越权限、顺序、非可修改区域或生成无用数据传输 | SG 合并对照 |
| DMA-010 | P0-F | descriptor fetch/完成写回不得与自己占住的数据资源构成依赖环 | 队列页放共享 SRAM 的压力用例 |
| DMA-011 | P0-F | 多片失败采用确定性聚合，保留首错与必要完成位图；错误日志溢出可见 | 多目标同时失败 |
| DMA-012 | P1 | transpose、layout transform、压缩/解压是单独能力；必须计入面积/能耗 | 关闭/开启对照 |

### 9.2 组播首版必须可用，Router 复制可分期

P0 功能基线采用 DMA/NIU 读取一份数据后，向多个 destination 发单播副本；同一个语义 API 后续可改为 Router 复制。它能减少源端重复读取，但未必减少所有 NoC link bytes，因此不能将源展开标成“硬件原生网络组播性能”。

| ID | 等级 | 要求 | 验收 |
|---|---|---|---|
| MCT-001 | P0-F | 支持 group 的创建/查询/版本化，目标集合固定于任务接收时 | 运行中组变更 |
| MCT-002 | P0-F | 每目的 buffer 空间与权限先确认；合法 destination 只能消费一次 | 慢目的/重复包 |
| MCT-003 | P0-F | TASK_DONE 需所有必需目的 TARGET_VISIBLE；任意失败输出失败/部分完成 | N 目标故障矩阵 |
| MCT-004 | P0-F | 一个目的失败不得静默从 expected mask 删除并宣布成功 | 目的断电 |
| MCT-005 | P0-C | 报告源 SRAM bytes、总 link bytes、注入 bytes、fanout 和端到端耗时 | 单播/源复制对比 |
| MCT-006 | P1 | Router 复制采用无环分发树或有完整依赖证明的算法；不能简单“XY＋全端口复制” | 组播依赖图 |
| MCT-007 | P1 | ACK 不在 Router 中无条件数数聚合；防重复、版本错配和部分故障需可证明 | ACK 重复/缺失 |
| MCT-008 | P0-F | Barrier 只同步参与者，不隐式等于内存 fence；到达事件须依赖 DMA 可见完成 | 事件先到数据后到 |

Gather 可由多个 DMA 写向不同区间实现。Reduce/All-reduce 优先通过 Tile/Vector 单元执行；Router P0 不实现 FP/INT reduction。P1 collective engine 必须另定义 dtype、累加精度、溢出/舍入、顺序确定性与数值验收。

## 10. Command/Event Fabric 与 Sync Event Unit

| ID | 等级 | 要求 | 验收 |
|---|---|---|---|
| CMD-001 | P0-F | 每 Tile 独立 command FIFO 与握手；一个 Tile 停顿不阻塞其他可接收 Tile | 一停多通 |
| CMD-002 | P0-F | 命令包含 context、epoch、task_id、opcode、descriptor 引用或内联参数、完成 token | 端到端身份 |
| CMD-003 | P0-F | Fabric 接收不等于 Tile 执行完成；accepted/started/done/error 分开 | 延迟执行 |
| CMD-004 | P0-C | 支持同周期向多个 Tile 投递；候选复制模式需报告真实 fanout 吞吐 | 多 Tile launch |
| CMD-005 | P0-F | command 的接收资源不能耗尽 completion/error 所需资源；先预留完成槽或可靠反压 | 完成队列满 |
| CMD-006 | P0-F | 不把中断当成逐 task 完成通路；中断用于聚合通知/异常 | IRQ coalescing |
| EVT-001 | P0-F | 完成事件按 context/epoch/task 去重，旧 epoch 事件不得递减新任务计数 | 迟到/重复事件 |
| EVT-002 | P0-F | 事件先于 waiter 注册到达时仍被保留或计数，不能丢唤醒 | arrival-before-wait |
| EVT-003 | P0-F | barrier 用 participation mask＋generation；每参与者只计一次 | 重复 arrive |
| EVT-004 | P0-F | 任务失败传播到依赖者，依赖者进入 failed/cancelled 而非永久等候 | 失败 DAG 片段 |
| EVT-005 | P0-F | event counter 饱和/下溢和 ID 复用检测可观测 | 边界/回绕 |
| EVT-006 | P0-F | Sync 不凭收到 command ACK 就发布 data-ready，必须使用第 3 节完成级别 | visibility litmus |

Sync Event Unit 提供可复用计数/等待/barrier 机制；Scheduler 负责 DAG 语义、资源许可、优先级与具体运行策略，两者不能维护彼此不一致的“任务已完成”事实。

## 11. QoS 与竞争力契约

### 11.1 工作负载分类

| 类别 | 典型数据 | 目标策略 |
|---|---|---|
| LATENCY | Decode demand KV、关键小读 | 有界包阻塞、预留接收资源、注入和 memory 优先服务 |
| COMPUTE_FEED | Prefill activation、weight tile | 持续吞吐、按资源份额调节 |
| BACKGROUND | KV 迁移、预取、回收复制 | token bucket、可暂停，不挤占保障份额 |
| RESPONSE | 返回/终结 | 由协议依赖定义资源保障，不由用户随意降级 |

不能让所有 Decode 请求无条件最高优先级且不整形。若 admission 总量超过瓶颈能力，任何 Router 优先级都不能创造确定性延迟。

| ID | 等级 | 要求 | 验收 |
|---|---|---|---|
| QOS-001 | P0-C | 注入端具备按 context/flow/class 的 rate/burst 调节和 outstanding 限制 | 可编程整形曲线 |
| QOS-002 | P0-C | 仲裁至少支持公平轮询和带权模式；提供 age/等待上界机制 | 服务份额＋最长等待 |
| QOS-003 | P0-F | 协议保留资源与用户 QoS 资源分离，优先级配置不能破坏前进性 | 最差 QoS 配置 |
| QOS-004 | P0-C | QoS 延续到 Memory NIU；memory 不配合时明确剩余干扰来源 | 逐段延迟归因 |
| QOS-005 | P0-C | 局部阻塞、请求注入、网络传输、target 等待、响应返回分别计时 | timestamp trace |
| QOS-006 | P0-F | 配置 admission 时对每个争用割集/目标校验保障率总和不超可持续容量，并留协议开销 | 配置 checker |
| QOS-007 | P0-C | 背景流在其最低服务保证已准入时不能无限饿死；关闭保障须显示为 best effort | 长时间 Decode 压力 |
| QOS-008 | P1 | deadline/拥塞反馈调节可选，失效时回退确定的静态模式 | 振荡/延迟反馈 |

### 11.2 建议验收目标（待 ESL 冻结）

下列数值是文王拟定门槛，不是业界统一标准。架构冻结前应通过 ESL 选择可达 profile；发布时不得保留“待定”并声称通过。

| 指标 | 提议门槛 | 测量条件 |
|---|---|---|
| 无竞争通道稳态吞吐 | 1 flit/cycle | 单输出、credit 足够、连续包、去掉启动/排空 |
| 长包有效带宽 | ≥同编码/路径可实现上界的 90% | 1KiB 及更长连续事务，计入头/掩码/credit 开销 |
| 拥塞混合流吞吐 | ≥经标定 ESL 可实现吞吐的 90% | 同 profile、种子、流量；不能拿注入需求当上界 |
| Decode p99 干扰 | ≤同配置独跑 p99 的 1.5×，作为初始探索目标 | Decode 自身低负载，背景在每个争用瓶颈受整形；memory 时延模型固定 |
| 带宽保障 | 在约定窗口内达到准入保障率减去公布容差 | 窗口至少覆盖最大包阻塞和 credit RTT；源持续 ready |
| Command 固定通路延迟 | 候选 ≤8 fabric cycles | 同钟、接收 FIFO 有空间；不含 Tile 执行/跨域延迟 |
| 无竞争 Router latency | 候选 1–3 cycles/hop | 不含外部 bridge/CDC，具体数依 STA |
| 工作负载价值 | 至少一个真实 Prefill/Decode trace 的收益可归因且无重大回退 | 对照简单 baseline，相同频率、端口、memory 能力 |
| PPA | 面积/能耗/布线约束内 Pareto 最优候选 | 相同 PDK、corner、V、f、floorplan、工具版本 |

真实 QoS 保证必须同时给出：arrival envelope（rate/burst）、目标服务假设、最大包长、参与 flow 数、预留资源和测量窗口。无法给出这些条件时，只能报告测得分位数，不能写“确定性实时”。

### 11.3 带宽算例：防止把 1024-bit AXI 当成 256-bit NoC 带宽

若一个方向的数据字段为 256 bit、f=1GHz，理想数据字段速率为 32GB/s；若 head 占同一个 256-bit 字段，一个 256B payload＋一个 32B head 的理想效率为 8/9≈88.9%，还未计算掩码、bubble、竞争和响应。

1024-bit AXI 一拍为 128B，经单条同频 256-bit 通道至少需要四拍数据字段，另加协议开销。要长期接收一拍一个 AXI beat，必须增加物理通道/位宽/频率，或接受 backpressure；加 FIFO 只能吸收短突发。

以 4 列×2 行网格为例，中间竖直切面有 2 条跨界链路；每条单方向 32GB/s，则该割集单方向原始上界为 64GB/s，不是八个 Tile 端口带宽之和。双向求和必须单列，不能冒充单向可用带宽。

在途容量按 `N ≥ ceil(B_target × L_roundtrip / payload_per_transaction)` 初估，然后加突发/公平调度余量。例如 32B/cycle、100 cycles RTT、256B/事务，需要至少 13 个满载在途事务才能覆盖理想 BDP；最终还受目标、tag、返回缓冲和包开销限制。

credit depth 同样需覆盖链路回环时延：一 flit/cycle 的稳定发送，至少需要约 credit RTT 个可流转槽位（确切差一拍由握手约定确定）。不能固定“4 deep”却承诺任意 pipeline 插入后满速。

## 12. Link Bridge、CDC、低功耗和恢复

| ID | 等级 | 要求 | 验收 |
|---|---|---|---|
| LNK-001 | P0-F | 支持同步 pipeline；异步域用异步 FIFO/等价可靠 CDC，不能直接同步多 bit payload | CDC 静态＋异步相位随机 |
| LNK-002 | P0-F | width conversion 保留包边界、VC、mask 和顺序；credit 按本地完整存储单位计 | 宽窄互转＋满队列 |
| LNK-003 | P0-F | async FIFO 的 reset/rejoin 为双端协议；单端复位不保留未经验证的 credit | reset 每种先后关系 |
| LNK-004 | P0-F | 动态频率变化只在允许的握手状态；DVFS/停钟期间在途数据与 credit 不丢 | 连续降升频 |
| PWR-001 | P0-F | `RUN→BLOCK_NEW→DRAIN→QUIESCENT→ISOLATED/OFF` 状态明确；超时不伪造 drain 成功 | 每状态 fault |
| PWR-002 | P0-F | DRAIN 包含 Router/FIFO/NIU tracker/ROB/DMA/completion queue，不能只检查 link idle | 隐藏 outstanding |
| PWR-003 | P0-F | 若 Router 为 transit 节点，不得仅因本地 Tile 空闲就关闭；需保留 transit 或系统改路由 | 中间 Tile power-off |
| PWR-004 | P0-C | ICG 门控覆盖空闲状态，唤醒/credit 初始化纳入性能与能耗 | idle→burst |
| PWR-005 | P0-F | wake-up 有管理侧可达通路；不能依赖已关电数据链路收包唤醒 | cold wake |
| PWR-006 | P0-F | quiesce 期间 blocking new 与拒绝/排队策略固定；所有入站 source 得知端点不可准入 | 在途/新事务交错 |
| REC-001 | P0-F | 非正常 reset 将相关 epoch 的在途事务标为 uncertain/aborted，并隔离迟到包 | 局部强制 reset |
| REC-002 | P0-F | epoch 回绕前必须保证旧包/响应完全消失；不能仅靠小计数器碰运气 | 快速复位＋延迟包 |
| REC-003 | P0-F | 不自动重放可能已产生副作用的写；retry 需 endpoint 支持去重/幂等或软件重新规划 | 重试不重复写 |

## 13. 安全、RAS 与可观测性

| ID | 等级 | 要求 | 验收 |
|---|---|---|---|
| SEC-001 | P0-F | 安全身份由可信 ingress 绑定；context 资源配额与可访问范围分别校验 | 身份伪造/越权 |
| SEC-002 | P0-F | 地址检查覆盖全事务而非仅首地址；组播每个目的独立验证 | 尾部越界 |
| SEC-003 | P0-F | 配置 CSR 有 privilege/lock；生产模式限制注错、trace 和权限改写 | 非特权访问 |
| SEC-004 | P0-F | 报文错误不会输出前一 context 缓冲中的残留数据 | 复用/错误 R 检查 |
| SEC-005 | P0-C | context 级 outstanding/带宽配额抑制 DoS；timing side-channel 完全消除不在 P0 承诺内 | 攻击流量＋受害流 |
| RAS-001 | P0-F | credit 下溢/溢出、非法路由、txn mismatch、超时均可捕获并定位 | 每项注错 |
| RAS-002 | P0-F | head/控制信息至少有可配置校验方案；数据/缓冲支持保护 profile，覆盖范围明确 | bit flip coverage |
| RAS-003 | P0-F | 数据校验失败不得当作成功数据发布；若已部分写入则标记 buffer invalid 并报告部分副作用 | 最后 flit 损坏 |
| RAS-004 | P0-F | P0 不做透明链路 retry；检测故障后隔离/终结；启用 retry 时另有序号、去重和 replay contract | profile 检查 |
| RAS-005 | P1 | Safety profile 可加 SECDED、控制冗余、诊断测试、FMEDA 输入 | 独立安全生命周期验收 |
| OBS-001 | P0-C | 每 port/VN/class 统计有效字节、flit、packet、stall、occupancy 高水位 | cycle 精确对照 |
| OBS-002 | P0-C | source/target/class 可过滤；latency histogram 给 p50/p95/p99，记录采样方法 | histogram golden |
| OBS-003 | P0-F | 计数器溢出行为固定；多字计数一致读取，支持 snapshot | wrap/read tearing |
| OBS-004 | P0-F | 首错和后续错误计数保留；日志满有 overflow sticky，不反压正常数据 | fault flood |
| OBS-005 | P0-C | 支持低开销 trace：context/task/tag/时间/路由/队列占用，默认不抓 payload | 性能扰动测量 |
| OBS-006 | P0-F | 管理访问在数据死锁或隔离时仍可返回合理状态 | 拥塞下 CSR |

## 14. CSR 与软件接口契约

下面为建议统一寄存器块模板。OR-DR 阶段只冻结语义；架构冻结后为每个 IP 生成精确地址和 field/reset/access 表。建议 32-bit CSR，64-bit 计数用 snapshot 或高低高一致读。

| 块 | 建议内容 | 规则 |
|---|---|---|
| IDENT/CAP | IP version、ABI version、profile hash、功能位、端口数、宽度、深度 | RO，与 RTL 编译配置一致 |
| CONTROL/STATUS | enable、block_new、drain、quiescent、reset reason | reset 默认禁止数据注入，管理可用 |
| IRQ | raw、mask、status、clear | sticky W1C，clear 与新事件同拍时保留新事件 |
| ERROR | first fault、overflow、txn/context/address、错误计数 | 有明确 latch/clear 原子语义 |
| QOS | class map、rate、burst、weight、minimum service | shadow 写入，校验后 commit |
| MAP | region base/limit、dst、attr、权限、version | drain 后原子提交；失败保留旧版本 |
| PMU | select/filter、snapshot、counters、histogram | 性能采样不得堵数据 |
| LINK | up/epoch、credit status、CDC/power status | 危险动作需 privilege/idle 条件 |
| DEBUG | trace、inject、freeze | 默认关闭，生产配置可裁剪 |

最小软件 API：`cap_query`、`configure_profile`、`submit_dma`、`submit_multicast`、`wait_event`、`fence`、`query_fault`、`quiesce`、`resume`。submit 返回句柄及 ACCEPTED/REJECTED；完成通过事件/队列给出 status、completion_level、partial metadata。

IP 的 reset 文档必须列出 FIFO、tracker、ROB、credit、group table、计数器、权限与映射的复位值/保留规则。不能只写“低有效复位”。

## 15. Generator / 参数与 profile

### 15.1 单一事实源

维护一个版本化 schema，生成 Python 数据类/模型、SystemC 参数、SV package、CSR 定义、C header、文档和校验报告。Generator 使用结构化 graph/IR 与可复用 SV CBB，避免字符串散拼和多个事实源。

配置需记录 topology、endpoints、address map、flow permissions、protocol profile、resource classes、buffer policy、QoS、clock/power domains、link pipeline、protection profile、placement hints。地址空间/IRQ/IP 版本可接入 SoC Studio；每次 generate 形成输入 hash、命令、工具版本、日志、产物摘要。

### 15.2 实验参数矩阵

| 参数 | 初始候选 | 冻结依据 |
|---|---|---|
| Tile/topology | 4 Tile：集中互连/2×2 对照；8 Tile：2×4；16 Tile：4×4 扩展 | 工作负载＋floorplan |
| 数据链路 | 128/256/512 bit，必要时 1024 或并行通道 | 割集带宽、能耗、布线 |
| 物理网络 | 共享物理链路＋隔离 VC；窄 REQ/RESP＋宽数据对照 | 小消息延迟与 PPA |
| 逻辑资源 | 第 5 节四类起点；每类 1/2 VC 探索 | 依赖证明＋HoL |
| FIFO depth | 2/4/8/16 flits 等候选 | credit RTT 与突发 |
| max packet | 4/8/16/32 flits | 头开销与尾延迟 |
| NIU outstanding | 8/16/32/64 等 | BDP、返回预留和面积 |
| Memory NI 数/位置 | 1/2/4 个候选 | memory 服务能力＋热点 |
| pipeline | 按 link 距离和 STA 插级 | 不凭全网统一 hop latency |
| multicast | source replication / router tree | workload 收益＋依赖安全 |

所有候选不是必须全部支持的 RTL 参数组合。ESL 允许广泛扫描；RTL 发布仅支持验收过的合法 profile，其他组合生成时报错。

### 15.3 生成时必须拒绝的配置

- 重叠且无优先规则的地址窗口、不可达目标、重复 endpoint ID、权限属性冲突。
- 禁止的转弯/路由环、协议资源依赖环，或缺少证明的资源合并。
- 不足以表达最大事务的 tag/长度/offset、非法位宽比、zero-depth 必需队列。
- 在途返回预留容量不足；credit 编码不足；bridge 容量单位不一致。
- 可关电 transit 节点无保留/旁路方案，管理唤醒不可达。
- QoS 保障超分配，或安全身份可被非可信端覆盖。
- 无法满足所选协议 profile 的 burst/USER/权限能力。

## 16. 验证、覆盖率与交付门禁

### 16.1 五层证据

| 层级 | 要证明的事情 | 必须产物 |
|---|---|---|
| Python semantic | 字节搬运、身份、分片、可见性、错误结果一致 | 可复用 golden、描述符测试、litmus |
| ESL resource | 带宽/延迟/HoL/credit/缓冲/目标瓶颈 | 参数矩阵、trace、结果与模型假设 |
| CBB/模块 | 局部协议、容量、仲裁、CDC、状态机 | UT、SVA/formal、lint/CDC |
| IP/子系统 | AXI/NoC/命令/事件端到端 | VIP、scoreboard、随机回归、fault injection |
| 物理/软件 | 时序/面积/功耗与真实调用 | synth/STA、代表性 P&R、软件样例、性能报告 |

### 16.2 必跑场景

1. 基本：每 src→dst、每方向、每 opcode、单/多 flit、空闲/满载、min/max 参数。
2. AXI：AW/W 先后错开、同/异 ID、不同 target、R/B 乱序、narrow/unaligned/sparse WSTRB、burst 分片、target stall。
3. 依赖：双方互读互写、所有 NIU 返回槽接近满、同 ID 跨目标、DMA descriptor/data 共存、长包竞争短包、credit 回程长延迟。
4. 流量：uniform random、nearest neighbor、transpose、hotspot、all-to-one、one-to-all、双向流、incast。
5. 模型 trace：Prefill GEMM 的 weight/activation 复用、Decode KV gather、小 batch 并发、KV migration、ViT 中间激活、state 更新。
6. 组播：fanout 1/2/N、一个慢接收者、一个永久故障、group version 变化、重复 ACK、部分目的完成。
7. 同步：事件先到、数据后到、barrier 重复到达、失败传播、上下文取消、buffer 提前复用。
8. 异常：非法地址/权限/token、包损坏、credit 错误、FIFO ECC、timeout、late response、tag/epoch wrap。
9. 电源：空闲/有在途/有 ROB 时局部复位、单端复位、异步时钟停止、DVFS、transit Tile 关电。
10. 配置：QoS/map 热更新、配置拒绝、管理路径满载诊断、counter snapshot、日志溢出。

### 16.3 RTM 与通过规则

每个本文件 requirement ID 映射到：LRS→功能项→HLD 责任块→LLD/RTL 模块→TC/断言→证据路径。P0-F/P0-C 不允许仅以文档链接当验证证据。可选项关闭使用明确 NOT_APPLICABLE，并验证能力位/拒绝路径；未跑为 NOT_RUN，不能等同 PASS。

| Gate | 退出条件 |
|---|---|
| G0 Contract | 范围、profile、语义、错误、所有权和 RTM 完整；无相互矛盾的完成定义 |
| G1 Function | Python golden 与边界/负向用例通过；OR-DR 冻结 |
| G2 Architecture | ESL 性能目标可达、依赖论证通过、资源预算明确、架构参数冻结 |
| G3 RTL | lint/elab/CDC 问题处理；UT、AXI VIP、端到端回归通过；关键安全断言无未解释失败 |
| G4 Robustness | 活性假设文档、fault/reset/timeout/multicast 压力通过；所有 P0 功能覆盖点命中，结构覆盖缺口有审核说明 |
| G5 Product | 同工艺 PPA/时序报告、软件样例、可复现配置、license/SBOM、发布包齐全 |

不能仅凭随机回归“跑了几亿周期没有死锁”宣称无死锁。形式证明必须列出 bounded/unbounded、环境公平假设、抽象边界和覆盖规模。允许模块性质＋依赖分析＋系统压力组合，但明确证明边界。

### 16.4 性能比较的公平性

同等端点能力、AXI 宽度、memory 服务曲线、工艺、电压、时钟和可用面积预算下，对照简单集中/分层互连和候选 Mesh。公开开源基线需锁定 commit、参数、license；商用产品没有实测同条件数据时，仅做功能对照。

报告必须包含 useful bytes、link bytes、header/mask bytes、read request/ACK 开销、吞吐、p50/p95/p99/max、queue occupancy、每阶段等待、面积、每 useful byte 能耗、wirelength、拥塞和关键路径。DDR 带宽已饱和时，不能将 NoC 局部提速宣传为端到端收益。

## 17. Agent 实现顺序和交付目录

### 17.1 推荐顺序

1. **先冻结语义边界**：实现共用 schema、byte-level golden、buffer ownership、completion/fence、错误与 abort 模型。不得先写 Router RTL 再补语义。
2. **完成 OR-DR**：逐项运行 SYS/AXI/TNI/MNI/DMA/CMD 的功能测试，厘清 BMU、Scheduler、Tile、Memory 的责任。
3. **资源受限 ESL**：建立输入 VC、credit RTT、packetization、NIU tracker/ROB、memory service 和独立 command/event。比较 2×4/256 起点与宽窄分离、集中/分层基线。
4. **冻结架构 profile**：选定包编码、VN/VC、路由、宽度、buffer reservation、inflight、QoS、CDC/power；完成依赖图和物理预算。
5. **最小 RTL 闭环**：CBB → 单 Router → 两 Router → source AXI NIU＋memory AXI NIU → byte-correct read/write；同阶段接入管理/错误路径。
6. **系统闭环**：Tile NIU＋DMA＋command/event＋sync；验证 data-visible 后再触发 compute。
7. **P0 性能和恢复**：多 memory NI、QoS、源端组播、PMU、CDC/DVFS/quiesce/fault；用真实 trace 验收。
8. **P1 增强**：Router 组播、ROB 并发优化、stream、collective、adaptive routing；每项独立 A/B，不突破 P0 接口与活性假设。

### 17.2 每个 IP 的发布目录建议

```text
ip/<name>/
  contract/       LRS、能力矩阵、错误/复位/完成语义
  design/         HLD、LLD、状态机、依赖图、资源预算
  schema/         配置与寄存器/协议 schema
  model/          Python golden、SystemC/ESL adapter
  rtl/            SV 模块与生成入口
  dv/             UT、VIP、formal、负向测试、配置矩阵
  sw/             C header、驱动示例
  reports/        RTM、回归、coverage、PPA、性能、CDC
  examples/       最小集成、2×4 子系统、故障恢复
  package/        FuseSoC core、IP metadata、license、manifest
```

建议 suite 顶层额外交付 `protocol_contract`、`system_generator`、`reference_system`、`performance_harness` 和 `release_manifest`。AI Agent 必须记录哪些功能已实现、已验证、仅建模、未运行；不得把本文件全文复制成“完成报告”。

## 18. 首版与增强版的取舍结论

**P0 必须交付**：稳定的非一致性数据事务层；边缘 AXI 完整语义；确定性路由和协议依赖闭合；并发 DMA/Tile/Memory 接入；独立 command/event；有效的 source multicast；可调 QoS；多 memory NI 能力；错误、reset/timeout/quiesce；PMU；统一生成与验证证据。

**P0 性能必须比较**：256 与 512-bit，共享物理链路与宽窄分离，包长/credit 深度/在途度，memory NI 位置，以及简单集中/分层 baseline。未比较就固定 2×4/256，不能证明竞争力。

**P1 优先**：Router 组播、跨目标重排并发、更细粒度 HoL 隔离、stream、布局提示与自动 pipeline；这些直接对应吞吐、尾延迟与交付效率。

**P2 审慎引入**：全局一致性、Router 内 reduction、自适应/故障绕行、透明重放和复杂压缩。必须先有负载收益证据与新增验证预算。

## 19. 参考资料与版本记录

以下为一手来源，检索于 2026-09-24。本文产品需求和数值目标是针对文王提出的设计，不是对来源的逐条翻译。动态网页/仓库应在研发启动时锁定 commit 或文档版本。

- [S1] Arm，*CoreLink NI-700 Technical Reference Manual*，Document ID 101566_0203_10_en，特别参见 Transaction tracking and ordering / QoS：<https://documentation-service.arm.com/static/64c2dc0438511951cb7a1956>
- [S2] Arteris，*FlexNoC 5 Interconnect IP Datasheet*；注意部分特性属于选项，URL 路径日期不等于当前 PDF 修订日期：<https://www.arteris.com/wp-content/uploads/2023/10/arteris-flexnoc-interconnect-ip-ds.pdf>
- [S3] ETH Zurich/University of Bologna，FlooNoC 官方仓库及论文入口：<https://github.com/pulp-platform/FlooNoC>
- [S4] FlooNoC 官方 Channels & Links 文档：<https://pulp-platform.github.io/FlooNoC/floonoc/links/>
- [S5] Berkeley Constellation 官方仓库：<https://github.com/ucb-bar/constellation>；Flow specification：<https://constellation.readthedocs.io/en/latest/Configuration/FlowSpec.html>
- [S6] Tenstorrent TT-Metalium，`noc_async_write_multicast` 官方 API：<https://docs.tenstorrent.com/tt-metal/latest/tt-metalium/tt_metal/apis/kernel_apis/data_movement/noc_async_write_multicast.html>
- [S7] Tenstorrent，Data Multicasting in matmul example：<https://github.com/tenstorrent/tt-metal/blob/main/tech_reports/prog_examples/matmul_multi_core_optimized/data_mcast.md>
- [S8] Arm，AMBA AXI and ACE Protocol Specification，IHI 0022H；以其中 AXI4 适用规则为本文外部协议参考，并非声称这是最新 AMBA 版本：<https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/IHI0022H_amba_axi_protocol_spec.pdf>；规范入口：<https://www.arm.com/architecture/system-architectures/amba/amba-specifications>

变更记录：v0.1 建立 IP Suite 范围、共用协议语义、逐 IP 需求、竞争力指标、验证和 Agent 实现顺序；待 Python/ESL 证据完成后分别冻结功能与架构 profile。
