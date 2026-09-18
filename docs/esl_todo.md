# ESL 资产开发计划与进度（唯一任务账本）

更新：2026-09-18。资产规格见 [esl_repo_plan.md](esl_repo_plan.md)，数值与场景预期见 [esl_testcase.md](esl_testcase.md)。本文件只跟踪工程资产；开发方法、模型目录原则和 Skill 进度由 esl-development-suite 管理，不作为本仓构建依赖。

## 当前状态与下一步

既有 Python 组件和 mini_pipeline 是历史原型/参考。现有源码不等于 SystemC 目标模型已交付；旧验收摘要仅作历史证据，本次不继承其全套 PASS。下方保留 R/X/A 等资产任务和判据，未链接本次有效证据的勾选只代表历史实现记录，当前验收以本表为准。下方历史 R/A 行提及的 Python common/models 路径现位于 reference/legacy_python；路径迁移以 repository_layout.md 为准。

| ID | 任务与下一步 | 状态 | 完成证据要求 |
|---|---|---|---|
| IP01 | 基础 IP 规划及 planned 登记 | 规划完成；B0 四模型及 timer/IRQ/UART/GPIO/DMA 已实现，其余 planned | [检查证据](../runs/planning-validation-2026-09-18/checks.json)；[唯一资产索引](base_ip_models.md)，注册状态以 registry 为准 |
| IP02 | 按 basic_system 的 B0 → B1 → B2 实现基础系统 | B0/B1/B2 已有证据；暂停新模型扩展，公共组件完成后再统一试用 | [B0 验证](basic_models_validation.md)，含源码/安装/搬迁消费者 |
| LAYOUT01 | 目标模型/系统示例/环境测试/历史 Python 分离 | 完成；SystemC/环境/迁移回归 PASS | [目录与兼容性](repository_layout.md)；[回归证据](../runs/layout-refactor-20260918/checks.json) |
| ID01 | 历史多段 VLNV 与公共校验的兼容迁移 | DMA 四段 ID/旧别名发现通过；其余 planned 资产在实现时迁移 | 保留旧 ID 解析/迁移说明，manifest/registry/消费者一致 |
| ENV03 | SystemC 3.0 系列最新补丁环境迁移 | 本机安装/构建/运行 PASS；模型集成另验 | [安装与冒烟证据](../runs/systemc-3.0.2-install/checks.json)；contracts/environment.json |
| SC01 | 先以四个 B0 基础模型交付目录/文档，后续 DMA/Compute | B0/DMA 完成；Compute 待实施 | 模型 manifest、公开 API、消费者入口 |
| SC02 | SystemC 库、有限资源和端到端 pipeline | 待验证/补齐 | 编译及真实事务、数据/生命周期检查；Python 测试不能替代 |
| SC03 | 上层 add_subdirectory 消费与多实例/背压/排空 | B0/B1/B2 消费者 PASS；其余扩展未验 | [B0 验证](basic_models_validation.md) |
| TOOL01 | inspect 能力详情、通用后端装配、sweep 完整参数域、真实 sanity checks | 审视修复完成：严格后端/失败退出、库模板、契约、证据哈希、安装文档；通用 SystemC 装配仍待实施 | [工具合同](../contracts/tool_contracts.md)、[消费者证据](basic_models_validation.md)、[工具回归证据](../runs/review-fixes-tool-checks/checks.json) |
| REF01 | 现有 Python 测试按算法参考/原型注明范围 | 已隔离 legacy_reference；独立饱和数值 oracle 和服务时间上下界检查通过；算法专项向量继续补齐 | reference/legacy_python；不声称 SystemC 目标时序通过 |
| PUBLIC01 | 公共资产四类规划与批次推进 | 当前主线；具体状态只在下方 PC/PI/PW/PA/PX 表维护 | 先公共合同/微基准，后统一试用完整模型 |
| MP01–MP07 | SRAM 专项 | 未验收 | 下方原专项判据及 M0–M8 |

推进顺序：公共批次 B-C0 → B-C1 → B-C2 → B-C3（下表列依赖）；公共能力完成后统一试用 DMA/UART 等消费者，再按需求恢复 B3 Compute/BMU 和多 AXI SRAM 专项。每次只更新对应任务行、证据和剩余缺口，不另起平行 TODO。

## 公共资产批次计划（当前执行主线）

本轮优先公共资产，覆盖建模组件、仿真基础设施、工作负载/验证、分析工具四类。先完成公共合同和独立 SystemC 微基准，最后统一试用完整模型；暂停 DMA/UART 功能扩展及 B3 Compute/BMU 推进。已验证的公共包子集和消费者已登记，未实现的扩展不随包一并宣称完成。

P0 是首批能力，不表示一个步骤内全部交付；P1 为首批稳定后扩展；P2 由实际精度/项目需求触发。`planned` 表示尚未交付；“实现中”表示该任务完整范围尚未完成；公共包可仅将合同明确、已验收的子集登记 available，不能把包状态当作整项完成。验收必须有源码/配置 hash、实际命令和结果，公共包状态以 registry 为准。下面各任务行是公共资产唯一进度源；后文 R/X/A 的同类条目仅保留历史编号和判据，不再并行更新状态。

### 批次与先后依赖

| 批次 | 交付边界 | 依赖与退出条件 |
|---|---|---|
| B-C0 合同与基础 | PI01–PI03、PI06、PI10、PC01、PC02 基础存储、PC03、PC06、PA00、PX01 | 事务/配置/事件与能力声明可执行校验；latency 与 II 独立；包可独立消费 |
| B-C1 多 Bank 探索积木 | PC04、PC05、PW01、PW05、PW06、PW08、PW09 | 独立仲裁/映射/流量微基准，不依赖完整 SRAM Controller 才能验证 |
| B-C2 离线分析闭环 | PA01、PA02、PA03、PA09、PA10、PA11 | 同一 SystemC 流量及事件生成地址映射图、Bank 热力图、事务时间线和批量比较报告；失败点不得消失 |
| B-C3 闭环与统一试用 | PI05 最小装配、PI07–PI08、PW03、PW04、PX02 | 闭环反馈、有限 buffer、drain/无进展诊断；完成公共组件后再集中接入模型并回归 |
| 后续扩展 | 其余 P1/P2 项 | 按实际消费者引入 AXI 细化、ECC、CDC、检查点等，不以占位目录算实现 |

B-C0/B-C1 无依赖的项可同批开展；其他批次不能绕过输入合同。特别禁止将 `latency=8, II=1` 实现成每 8 拍接收一次：必须检查首完成延迟、稳态吞吐、在途容量和输出背压。

本轮存储可靠性验收：公共/模型 [集成证据](../runs/common-reliability-final-20260918/checks.json)、[模板证据](../runs/common-reliability-template-20260918/checks.json)、[NPU 消费者证据](../runs/common-reliability-npu-20260918/checks.json)。PC02/PC10/PC11/PW04 按各行限定服务范围收敛，其他任务状态不继承。

### 1. 建模基础组件

| ID / 优先级 | 公共能力与首版边界 | 复用对象 / 依赖 | 状态与完成判据 |
|---|---|---|---|
| PC01 / P0 | FIFO、有限容量、credit、watermark、占用与拒绝统计；存储项、在途项和未取完成项口径明确 | DMA 队列、SRAM 请求/返回队列、路由缓冲；PA00 | 实现中：BoundedQueue、Gate/Lease 和积分已有 13 项公共微基准，见 runs/common-batch-first-20260918/checks.json；credit 守恒与统一公开合同仍待收敛；满/空/异常/释放/背压无丢失或重复 |
| PC02 / P0→P1 | 存储服务：字节存储、RAM/ROM、初始化、byte-enable、地址边界；稀疏后端 P1 | SRAM、外存、descriptor RAM；PI01 | 已完成当前存储服务范围：ByteStore/SparseStore 统一初始化、contains、burst、循环 byte-enable、clear；RAM/ROM 沿用存储与权限分层。跨页/禁用字节/非法请求与稀疏大地址经两后端同合同验证。证据见本轮存储可靠性验收。 |
| PC03 / P0 | 参数化资源/流水线：latency、II、实例数、有限在途/输出容量、资源预留分开 | Bank、ECC 编解码、计算占位；PC01、PI01、PA00 | 实现中：ResourceTiming 已独立验证 latency=8/II=1、双实例、未消费结果保留 credit；组合比较 II=1/8。 |
| PC04 / P0→P1 | RR、固定优先级首版；加权 RR、年龄保护及带宽份额 P1 | Bank、总线、任务调度；PI06、PC01 | 实现中：Arbiter 已实现 RR/WRR/固定优先级/年龄保护，解析份额和持续竞争 fixture 通过；权重按 grant 计，不宣称 byte 带宽公平。 |
| PC05 / P0 | 分区、交织、XOR；bank/group/row/offset 正向映射与受支持配置的逆映射 | 多 Bank SRAM、分布式存储、映射可视化；PI02 | 实现中：AddressMapper 分区/交织/XOR、bank/group/local/stripe-row 与逆映射通过全地址双射和热点测试；区域表仍待公共化。 |
| PC06 / P0 | 寄存器服务：译码、字段权限、复位值、W1C、读写副作用/回调、dump | 配置、状态、IRQ；PI01–PI02 | 实现中：RegisterBank 已有 RW/RO/W1C、strobe、回调/reset/dump fixture；跨仓 SystemRDL 生成 ESL 描述符仍待完成。 |
| PC07 / P1 | burst→beat→fragment、跨 bank/宽度拆分、mask、完成重组 | 多 AXI SRAM、宽窄转换；PC05、PI01、PC08 | 实现中：跨边界 split_transaction 与 CompletionAssembly 已测非整除、失败汇聚/重复完成；跨 Bank mask 重组组合待补。 |
| PC08 / P1 | transaction ID、同 ID 顺序、ROB、依赖与完成屏障 | AXI 返回、多通道 DMA、任务调度；PC01、PI01 | 实现中：OrderedCompletion 有限 ROB、同 stream 退休/跨 stream 并发和屏障已有 fixture；reset epoch 组合待补。 |
| PC09 / P1 | 带宽链路、pipeline、credit、请求/响应通道与可组合交换路径 | Crossbar、分层 Router、NoC；PC01、PC03–PC05 | 实现中：TimedChannel 独立序列化/流水延迟/消费后 credit 返回已验证；多跳路由与链路观测待组合。 |
| PC10 / P1→P2 | ECC/错误服务：注错、纠错状态、RMW、scrub、编解码资源；具体码型按需求 | 可靠 SRAM、功能安全探索；PC02–PC03、PW08 | 已完成 64+8 SECDED 服务范围：EccMemory 支持部分写 RMW、corrected/uncorrectable 传播、单字 scrub 修复/注错及实际读写/编解码计数；dense/sparse 共享 SystemC bank/codec 干扰与更新不丢失通过。周期、地址巡检与多 Bank 仲裁由 owner 组合，不声称物理可靠性验证。 |
| PC11 / P1 | 可组合 pending/mask/clear、事件合并、门限触发 | DMA 完成、外设中断、错误上报；PC06、PI01 | 已完成当前中断状态服务：pending/mask/clear、popcount 门限、边沿/电平采样、同采样新事件优先于 W1C、reset 历史清空；独立 fixture 与原消费者回归通过，SystemC 信号驱动归 owner。 |
| PC12 / P2 | 时钟比、跨域队列、同步可见延迟、复位清空策略 | 多时钟 SRAM/互连；PC01、PC03、PI07 | 实现中：ClockDomainQueue 明确目的时钟边沿与同步可见周期，reset 返回丢弃数；独立 fixture 已验证，不宣称物理 CDC 验证。 |

### 2. 仿真基础设施

| ID / 优先级 | 公共能力与边界 | 依赖 / 复用对象 | 状态与完成判据 |
|---|---|---|---|
| PI01 / P0 | 统一事务/结果：地址、长度、mask、ID、parent/source、QoS、错误、epoch、阶段时间戳 | 所有组件、适配、事件、scoreboard | 实现中：Transaction/TransactionMetadata 与同步 TLM binding，拥有数据/mask 并恢复旧扩展；尚不代表完整 AXI 协议。 |
| PI02 / P0 | 配置 schema 与参数/容量/连接/位宽/映射约束 | 组件、工作负载、扫描；TOOL01 | 部分已有：模型/registry/参考后端配置校验；公共 SystemC 配置 planned；未知参数、冲突、溢出、不可用组合在运行前拒绝并定位字段 |
| PI03 / P0 | 公共组件能力声明、协议/profile/精度/限制与依赖 | inspect、装配、报告；PX01 | 部分已有：model.yaml/证据状态；公共包 common.yaml 实现中；声明有真实入口/用例，planned 不能被执行器当可用 |
| PI04 / P1 | TLM/simple memory/stream/task/trace 适配；AXI 抽象按明确子集扩展 | PC07–PC09、PW02 | 实现中：同步 TLM 事务适配已实现/验证；其余 stream/task/可重放 trace 适配待补。 |
| PI05 / P0→P1 | 配置装配、拓扑与未连接/方向/协议/位宽/地址冲突检查 | PI01–PI03、PC05；TOOL01/R33 | planned；先支持已实现组件的有限拓扑，输出最终解析配置；不能静默回退 legacy Python；所有权/时间 owner 检查不声称静态证明任意代码 |
| PI06 / P0 | seed、随机子流、实例标识、同刻稳定 tie-break | 合成流量、仲裁、注错、扫描 | 实现中：DeterministicRng 使用固定 SplitMix64/FNV-1a 命名子流，重放/恢复/有界采样通过。 |
| PI07 / P0 | 初始化→预热→测量→停止注入→drain→结束 | 所有系统、工作负载、统计窗口 | 实现中：SimulationLifecycle 明确 warmup/measure/drain/finish，未排空不能完成；独立 SystemC fixture 已验证。 |
| PI08 / P0 | 无进展/watchdog、队列与资源快照、等待关系 | PC01/PC03、PI07、PA00 | 实现中：ProgressWatchdog 保留等待对象/原因/截止时刻，长服务不误报与超期停滞已测；系统队列快照接入待补。 |
| PI09 / P2 | 检查点与恢复 | PI01/PI06/PI07、所有可序列化组件 | planned，晚做；需覆盖 SystemC 进程、未完成事务、事件、随机状态及依赖版本；仅保存 RAM 内容不称完整恢复 |
| PI10 / P0 | run manifest：最终配置、版本/dirty hash、依赖、workload、seed、窗口、状态/结果绑定 | 验证/扫描/可视化/校准 | 部分已有：命令、日志、源码 hash、证据失效；统一 workload/seed/窗口 schema planned；离线可追溯，输入变化使证据失效，失败 run 不覆盖 |

### 3. 工作负载与验证组件

| ID / 优先级 | 公共能力与边界 | 依赖 / 复用对象 | 状态与完成判据 |
|---|---|---|---|
| PW01 / P0 | 合成流量：顺序、随机、stride、热点、突发、读写混合、端口相位 | PI01/PI02/PI06；Bank/互连/DMA/存储 | 实现中：TrafficSource 顺序/stride/随机/热点/读写混合已测；多 Bank 组合实际有限 outstanding 反馈。突发与端口相位配置待扩展。 |
| PW02 / P1 | 标准 trace reader/writer、录制/回放 | PI01、PA00、PI06 | planned；版本/时间单位/依赖/source/截断标记，真实流量和仅观测 trace 区分；回放保持语义，不将缺依赖 trace 当可重放 |
| PW03 / P0 | 闭环 DAG 引擎、有限 buffer、完成反馈、load/compute/store 重叠 | PC01/PC03/PC08 基础依赖、PI07 | 实现中：TaskGraph 已有确定性就绪、有限并发/output buffer、末消费者释放、失败传播/环拒绝/容量停滞 fixture；新增两个有限 load/compute/store SystemC 组合，解析完成时间 13 ns 与失败隔离通过。 |
| PW04 / P0 | 计算资源占位：计算量/吞吐、latency/II、有限并发、完成事件 | PC03、PW03 | 已完成同尺寸任务计算占位：ComputeTiming 按工作量/吞吐向上取整并叠加流水级，独立配置 II/实例/容量，拒绝非法值/时间溢出；独立解析 fixture 与 task_pipeline 两种消费者通过。变长任务需 owner 调度，不修改活动资源延迟。 |
| PW05 / P0 | scoreboard：数据、mask、映射、ID 顺序、最终状态 | PI01、PC02/PC05、PW01 | 实现中：多 Bank fixture 有独立地址/最终数据 oracle；MemoryScoreboard 已实现独立 mask oracle，并接入组合的最终数据检查。 |
| PW06 / P0 | 协议/资源检查器：非法请求、重复完成、容量/带宽、事务/字节守恒 | PI01、PA00、PC01/PC03 | 实现中：公共接口拒绝重复/未知/提前完成与超容量，CSV 分析检查事务守恒；ConservationChecker/BandwidthChecker 已实现，独立负向注入和组合守恒通过；完整协议监视器待扩展。 |
| PW07 / P1 | NPU 访存：shape/layout/tiling/GEMM/Attention/转置 | PC05、PW01/PW03 | planned；作为独立 workload，不绑定单一 SRAM；地址/字节量/依赖/结束条件自检，支持 stride/padding 对照 |
| PW08 / P0 | 可重放背压、错误、bank 暂停、延迟扰动、带宽下降 | PI06、PI07、PC03、PW06 | 实现中：FaultSchedule 明确目标/半开时间窗，暂停/错误/延迟/带宽效果分离；组合实际应用 Bank 暂停恢复和 DAG 分支错误。随机窗口与其他效果接入待扩展。 |
| PW09 / P0 | 解析微基准与独立公共 fixture | 每项公共组件、PA00 | 实现中：examples/common_primitives 已扩为 46 个 SystemC 场景，新增存储/ECC 时序与计算/中断策略；完整模型集成 113 个独立场景、323 次执行通过。后续公共能力继续新增解析 fixture。 |
| PW10 / P1 | RTL/测量 trace 校准、误差计算、训练/验证分离 | PI10、PW02、PA10 | planned；真实参考到位后定义误差指标/适用范围，不能用自身模型拟合自身或预设无依据误差门限 |

### 4. 统计、可视化与架构探索

| ID / 优先级 | 公共能力与边界 | 依赖 / 首要用途 | 状态与完成判据 |
|---|---|---|---|
| PA00 / P0 | 统一事件/统计 schema、counter/gauge/span、占用/忙时/吞吐；off/counters/trace | PI01/PI06/PI10；全部组件和分析工具 | 实现中：ActivityMonitor + EventRecorder；off/counters/trace、整数 tick/parent/source/resource、截断标记与完整计数；通用 span/gauge 接入待补。 |
| PA01 / P0 | 地址映射查看器 | PC05、PW01/PW07、PA00 | 实现中：common_explore 离线报告显示实际请求 address→bank，8 组交织/XOR 扫描；tensor/padding 交互视图待扩展。 |
| PA02 / P0 | 事务时间线 | PI01、PA00 | 实现中：统一 CSV 服务起止时间线与事务关联已可离线查看；排队/路由/重组阶段等待原因待补。 |
| PA03 / P0 | Bank 热力图 | PC03/PC05、PA00 | 实现中：Bank 时间窗平均活动事务热图已落地，明确区别 busy 利用率；读写/RMW/冲突细分待补。 |
| PA04 / P1 | 链路热力图 | PC09、PA00 | planned；吞吐、拥塞、credit stall、跨组流量；请求和返回链路分开，不能把 bank 空闲推断成无瓶颈 |
| PA05 / P1 | 队列监视器 | PC01、PA00 | planned；占用/高水位/full 周期/HOL/credit 等待；“满”与“无服务”分开解释 |
| PA06 / P1 | 拓扑查看器 | PI03/PI05 | planned；端口、组件、bank、链路、位宽、容量来自最终配置；避免手绘拓扑与实际装配漂移 |
| PA07 / P1 | NPU 执行时间线 | PW03/PW04/PW07、PA02 | planned；load/compute/store、buffer 等待、计算空闲；重叠区间按交集/并集计算不双计 |
| PA08 / P1 | 延迟分析器 | PA00、PW01/PW02 | 实现中：离线 nearest-rank p95/p99 与截断禁止完整延迟结论，协议负向测试通过；尾部等待原因待补。 |
| PA09 / P0 | SystemC 参数扫描 | PI02/PI06/PI10、PW01、PA00 | 实现中：common_explore 对 8 组实际 SystemC 配置各运行 off/trace，保留逐点失败/日志/事件；通用配置扫描待扩展。 |
| PA10 / P0→P1 | 方案比较、最差退化、资源代理指标与 Pareto | PA09、PI10 | 实现中：有效点与串行基线完成时间比较；资源代理/Pareto 与跨 workload 最差退化待补。 |
| PA11 / P0 | 自动离线报告与制品归档 | PA01–PA03、PA09–PA10、PI10 | 实现中：results.json + 日志/CSV + 离线 report.html 已生成，绑定可执行文件和脚本 SHA256；更完整运行来源绑定待补。 |

三个首要图形工具固定为 PA01 地址映射、PA03 Bank 热力图、PA02 事务时间线。它们消费同一份事件与统计合同，不分别复制解析器和单位转换；Python 可做离线分析/可视化/批次编排，不代替 SystemC 模型或另造仿真内核。

### 5. 组织、组合与统一试用

| ID / 优先级 | 工作 | 状态与退出条件 |
|---|---|---|
| PX01 / P0 | 公共目录分层、manifest、标准包与安装导出 | 实现中：AixEslCommon 草案与独立消费者；目标职责见下表。只随真实资产创建目录；迁移保留稳定公开 include/target，旧实现移除而非复制；源码/安装/搬迁三种消费验证 |
| PX02 / P0 | 公共组件完成后统一试用，模型仅承担消费者角色 | 实现中：本轮存储可靠性独立验证与共享资源试用完成；公共/现有模型 source/install/relocated 回归 113 场景、323 次执行通过，证据 runs/common-reliability-final-20260918/checks.json。完整余项继续按 TODO 推进。 |

| 目标目录职责 | 资产归属与迁移约束 |
|---|---|
| contracts/ | 事务、接口、配置、事件/统计及运行记录格式，唯一事实源 |
| primitives/ | 队列、credit、仲裁、映射、流水线/资源；现有 common 基础实现按 PX01 单次迁移 |
| adapters/ | TLM/AXI 子集、memory/stream/task/trace 适配 |
| services/ | 寄存器、存储、ECC、中断，功能服务与时序策略分离 |
| workloads/ | 合成流量、闭环 DAG、NPU 访存与计算占位 |
| verification/ | scoreboard、协议/资源检查器、注入、解析微基准/校准 fixture |
| tools/ | 离线查看、统计聚合、扫描、比较、报告；共享格式解析 |
| models/ | DMA、SRAM Controller、BMU 等完整模型，组合公共能力，不复制其实现 |
| systems/ | 组件组合；现有 examples 的实际 CMake 消费者路径在迁移验证前保持可用，不维护两份顶层 |
| common/ | 现有公共包构建/文档入口；分层后可保留聚合 target，不变成上述目录的第二份实现 |

首批共同验收约束：同一存储功能至少接两种时序策略做数据差分，仲裁策略可替换而功能不变，观测开关不改变数据/目标时间；公共 API 至少展示两个不同用途的消费者（可为独立 fixture），并说明不能合并的业务边界。latency、II、带宽和有限容量分别校验；源码路径不可泄漏到导出包。对多 AXI SRAM 的专项 M0–M8，公共微基准只是前置证据，不能自动替代完整专项验收。

历史对应：R07/R09/R10→PC01/PC04/PC02；R08/X05→PC03；X03→PC06/PC11；R12→PW03；R19–R23→PA00/PA02；R27→PA09/PA10；R33→PI05；X01/X07/X10→PC05/PC07–PC09。历史 Python PASS 不继承为本表 SystemC PASS；未列出的 R/X 专项保持原始历史参考身份。

## 3. Repo 基础 TODO（历史冻结清单）

### A. 环境与最小接口

| 历史记录/待验 | ID | TODO | 完成条件 |
|---|---|---|---|
| 历史实现 | R01 | 建立仓库目录、CMake/Python 入口、最小 registry | 示例依赖能解析；不要求独立资产服务器 |
| 历史实现 | R02 | 固定一套 Linux 环境配方，实现 doctor | 干净环境可配置构建；缺依赖能指出具体缺项 |
| 历史实现 | R03 | 定义精简 model/system/experiment 配置格式 | 统一model_kinds与profiles映射；参数、能力组合、未知字段和非法连接可检查（common/base/config.py） |
| 历史实现 | R04 | 实现配置解析与模型 Factory | 输出最终生效值；未知模型/端口拒绝，参数不静默丢失（ModelFactory） |
| 历史实现 | R05 | 固定 Command/Memory/Event 最小合同 | 明确接受/重试/终结、地址空间、byte enable、取样/可见性、payload与仲裁owner（common/transport/contracts.py + MemoryPort） |
| 历史实现 | R06 | 提供实例 ID、整数 tick、epoch 与基础生命周期 | 同刻处理顺序明确；能够 reset、drain、终止 |

R03/R05 只需要短说明和可执行校验，不要求完整规范书。实际字段、错误码和工具接口由 Repo 维护，Skill 只引用。

### B. 公共行为与资源组件

| 历史记录/待验 | ID | TODO | 完成条件 |
|---|---|---|---|
| 历史实现 | R07 | BoundedQueue | 深度只计等待项；满队列 retry 不丢失/重复请求 |
| 历史实现 | R08 | 非流水 ServiceResource | 有限容量、排队、服务起止与释放；单 server 不重叠 |
| 历史实现 | R09 | 基础仲裁策略 | FIFO 与确定性选择；支持示例的 ready store 优先规则 |
| 历史实现 | R10 | ByteStore + MemoryPort | 真实数据、地址边界、byte enable、完成时可见 |
| 历史实现 | R11 | BufferPool | 原子 slot 分配、满池等待、generation、重复释放检测 |
| 历史实现 | R12 | Event/TaskGraph | 依赖状态检查、唤醒、终结唯一性、上游失败传播 |
| 待验 | R13 | 公共排空/取消支持 | 活动访问结束前不复用 buffer；旧 epoch 不污染新任务 |

每个组件只写少量能够区分对错的单测。优先测容量边界、时间顺序和异步清理，不为 getter/setter 堆测试。

### C. 基础模型

| 历史记录/待验 | ID | TODO | 完成条件 |
|---|---|---|---|
| 历史实现 | R14 | 命令式 DMA | 真实copy；行为核心可在functional与resource profile复用；load/store共享资源（骨架已建，T01数据正确；完整周期对齐待后续） |
| 历史实现 | R15 | ComputeShell + vector_affine | int32中间值、int16饱和；functional与resource复用核心，不强求纯功能模型依赖SystemC（含独立oracle） |
| 历史实现 | R16 | BMU 模型 | 包装 BufferPool；占用、等待、释放可观察（骨架已建） |
| 历史实现 | R17 | 简单 Scheduler + Host | 提交 load→compute→store DAG；不模拟 CPU 指令（骨架已建） |
| 历史实现 | R18 | mini_pipeline 组合资产 | 地址/slot/任务关系在配置中；不复制公共组件源码（骨架已建，T01数据正确，T01/T02/T03周期 77/待 对齐） |

### D. 性能观察与测试资产

| 历史记录/待验 | ID | TODO | 完成条件 |
|---|---|---|---|
| 历史实现 | R19 | Counter/Gauge/Span/Event 公共 API | 目标时间统一；关联 ID 放 trace，避免指标标签膨胀（observability/core.py） |
| 历史实现 | R20 | Queue/Resource/Memory/Buffer 自动埋点 | 模型不重复实现 busy、queue 积分和容量统计（instrument.py） |
| 历史实现 | R21 | 指标聚合器 | 正确计算计数、busy、占用、吞吐与overlap；whole_run含最后完成，零窗口不除零（A06–A09 通过） |
| 历史实现 | R22 | Trace 与时间线导出 | 任务可关联；tick 单位换算正确；并发轨道合法（A09 通过） |
| 历史实现 | R23 | 观察开关与失败环形缓冲 | off/counters/trace 不改变目标行为；截断有标记（A10 通过） |
| 历史实现 | R24 | ScenarioRunner + 独立 oracle | 可定位首个数据差异；不调用被测功能当参考（harness.py） |
| 历史实现 | R25 | 确定性错误/取消/复位注入 | 可指定 chunk/事件边界；失败可重放（FaultInjector） |
| 历史实现 | R26 | 性能自洽检查 | 检查字节守恒、容量、资源下界与完成先后（SanityChecker） |
| 历史实现 | R27 | SweepRunner + RunComparator | 保留全部点与失败原因；比较实际配置及同口径指标（sweep.py，E0/E1 实跑） |
| 历史实现 | R28 | 自动结果与简报 | run/metrics/checks/summary完整；执行状态、验收状态、外层test_status分别记录（reporting.py） |

### E. 模板与统一入口

| 历史记录/待验 | ID | TODO | 完成条件 |
|---|---|---|---|
| 历史实现 | R29 | compute / command_engine / buffer_manager / memory_target 模板 | 每个生成物能独立构建并执行一个实际小案例（+ compute_cpp SystemC 变体：cmake build+run 验证） |
| 历史实现 | R30 | pipeline_system / parameter_sweep / report 模板 | 从模板创建新名称实例可运行；不依赖原示例目录名（esl new system demo_pipe 直接运行 4864 ticks PASS） |
| 历史实现 | R31 | esl inspect / new / run / sweep / compare / bench 入口 | inspect返回实际available能力；run按profile执行适用构建/检查/摘要（run E0=4864/E1=2592 PASS） |
| 历史实现 | R32 | 环境、组件和示例 README | 说明参数、用途、限制及重跑命令；不重复维护方法体系（common/README + docs/environment.md + mini_pipeline/README） |

R29 不要求把 Repo 规划中的全部模板一次做完；DMA 模板可在 R14 稳定后从其薄骨架提炼，寄存器与 bank 模板归扩展项。



## 6. 基础验收：只看三件事

1. **配置合法：**支持的配置能运行，非法配置及时拒绝。
2. **结果正确：**数据、依赖、完成与资源生命周期一致。
3. **性能自洽：**时间与资源约束符合模型约定，指标能够解释实验。

基础主体为 `pipeline_analytic + full_data`；另用小样本functional profile做类别/跨profile检查，不增加第二套模型核心。CPU 指令执行、真实 descriptor fetch、bank 时间与 RTL 校准均不在该范围内。不能把这些未覆盖项目写成已通过。

### 6.1 Repo 公共能力验收

| ID | 项目 | 操作与预期 | 关联 TODO |
|---|---|---|---|
| A01 | 干净环境与 CLI | 按 README 配置一次；通过 doctor，直接运行示例，无需 Skill | R01–R04、R31–R32 |
| A02 | 队列/资源 | 深度=1 持续施压；不丢/重复，等待项≤1，单 server 服务不交叠 | R07–R09 |
| A03 | 内存 | 部分 byte enable、末地址、越界访问；内容和错误符合合同 | R05、R10 |
| A04 | BMU 生命周期 | 池耗尽后等待，释放后恢复；重复释放报错；旧 handle 无效 | R11、R13、R16 |
| A05 | 事件与依赖 | 先完成后等待仍可察觉状态；失败依赖不执行后继 | R06、R12、R17 |
| A06 | 指标积分 | 合成 fixture：queue 深度在 [0,2)为0、[2,5)为2、[5,10)为1，均值=1.1，峰值=2 | R19–R21 |
| A07 | 时间与利用率 | 单 server 忙区间 [2,5)，窗口 [0,10)，busy=3、utilization=0.3 | R19–R22 |
| A08 | overlap 算法 | DMA=[0,4)、[6,10)，Compute=[2,8)，总重叠=4；不能重复累计 | R21 |
| A09 | 时间线单位 | 1 ns/tick 的1000 ticks事件导出为1 μs；关联与轨道正确 | R22 |
| A10 | trace 截断 | 限制为少量事件，显式报告丢弃；完整 counters 与未截断运行一致 | R23 |
| A11 | 生成模板 | R29/R30 每种模板各生成一个新名称资产，构建并运行最小例子 | R29–R30 |
| A12 | 结果状态 | execution正常但oracle失败时overall=FAIL；负向harness验证符合预期后test_status=PASS，原run仍FAIL | R24、R28、R31 |
| A13 | 用途/profile能力 | 实现functional/resource映射；不支持bank/traffic_only等选择拒绝；planned不可执行 | R03–R04、R31 |
| A14 | 跨profile功能 | 同一小输入的functional与pipeline_analytic数据/终态一致；前者性能检查不适用 | R14–R18、R24 |
| A15 | 窗口端点 | whole_run包含最后完成；相邻半开窗口不双计；零时长性能为null/不适用 | R21、R28 |
| A16 | 轻量行为模型 | 用纯Python行为fixture执行oracle，无需SystemC；Schema不能强制所有模型CMake/计时 | R03、R24、R31 |

A06–A09 使用短小人工可计算 fixture，不依赖 mini_pipeline 的实现来证明观察器正确。

### 6.2 Mini Pipeline 基础验收

完全沿用配套规划的 T01–T12；此处给执行清单，不重新定义模型语义。

| 历史记录/待验 | 用例 | 通过条件 |
|---|---|---|
| 待验 | T01：单完整 chunk/单 slot | 256元素、输出逐项一致；76 ticks |
| 待验 | T02：64 chunk/单 slot | 16,384元素全部一致；4,864 ticks；DMA/Compute 不重叠 |
| 待验 | T03：64 chunk/双 slot | 输出一致；2,592 ticks；存在正确的搬运/计算重叠 |
| 待验 | T04：DMA queue=1 | slot=1和2均不丢/重复任务，无容量越界；不强制沿用默认队列的周期 |
| 待验 | T05：尾块 | N=1/255/257 的数据与 guard 区均正确 |
| 待验 | T06：非法配置 | slot超容量、queue=0在仿真前拒绝，指出具体字段 |
| 待验 | T07：load失败 | 后继compute/store不执行；按fail-fast规则终结并排空 |
| 待验 | T08：store失败 | 不计成功；活动访问排空后释放slot |
| 待验 | T09：取消/复位 | 分别覆盖DMA和Compute活动期间；无旧epoch污染、悬挂引用或提前复用 |
| 待验 | T10：观察档位 | off/counters/trace下的数据与目标仿真时间一致 |
| 待验 | T11：重跑 | 相同配置/seed目标数据、调度、指标一致，wall time和run_id可不同 |
| 待验 | T12：故意错误的oracle | 被测run报告FAIL并定位首个差异；外层负向测试符合预期才PASS |

精确周期只适用于配套文档的聚合时间、默认数据规模、零时间控制、默认调度策略与队列设置。它们是解析测试预期，不是芯片指标，不可套用于 memory_resource。

以下统计要求适用于counters/trace档；off只检查功能、终态和目标时刻，性能采集记NOT_RUN。默认64 chunk成功运行还必须同时满足：

- 完成64个chunk，load/compute/store各64次，每个任务只完成一次。
- 外存读32 KiB、写32 KiB；SRAM总逻辑访问128 KiB；输出有效字节32 KiB。
- DMA累计busy=2,560 ticks，Compute累计busy=2,304 ticks。
- 结束时slot占用为0，活动请求为0；峰值不超过配置slot数量。
- 每个chunk均满足load完成≤compute开始、compute完成≤store开始、store完成≤slot释放。
- 单server上无服务区间交叠，成功完成不早于数据可见。

### 6.3 参数探索与报告验收

执行配套 E0–E6 共7个点：slot=1/2/4，双slot下改变DMA带宽及Compute lane数。禁止静默缩小参数范围或丢弃失败点。

| ID | 判据 |
|---|---|
| P01 | 每个点保存实际生效配置、数据模式、profile、运行/正确性状态 |
| P02 | E0/E1满足上述解析预期；其他点数据正确并满足各自资源下界 |
| P03 | 比较表包含总ticks、samples/tick、DMA/Compute利用率、BMU峰值和speedup |
| P04 | 同口径成功运行才计算speedup；失败/超时点明确展示，不能填0耗时 |
| P05 | 至少用一段时间线解释双缓冲减少了什么等待、为何不是无限加速 |
| P06 | 报告区分“资源模型内的结果”和真实芯片预测；说明未建模bank/descriptor/RTL细节 |
| P07 | 比较不同workload/profile/统计窗口时给出差异，不能伪装成只改一个参数 |

不硬性要求每个优化都改善性能。没有收益甚至变差，只要结果正确、调度明确且解释有证据，同样可以通过探索方法验收。



## 8. 扩展 TODO 与验收

| 历史记录/待验 | ID | 扩展任务 | 启动条件 | 验收 |
|---|---|---|---|---|
| 待验 | X01 | BankedMemory + banked_sram模板 | 要比较SRAM bank/端口 | 配套同bank=16 ticks、异bank=8 ticks微基准；跨bank请求正确拆分 |
| 待验 | X02 | memory_resource profile | 聚合DMA时间不足以解释访存 | 删除被替代的聚合搬运计时；真实事务/竞争，字节守恒；不套用4864/2592 |
| 待验 | X03 | RegBank/IRQ + register_device模板 | 要测试软件控制/寄存器前端 | RW/RO/W1C、byte strobe、mask、同刻set优先、busy重复START与reset |
| 待验 | X04 | DescriptorCodec + fetch前端 | 要分析描述符访问开销/兼容性 | 字节布局固定；非法字段拒绝；记录fetch流量；与命令模式功能一致 |
| 待验 | X05 | PipelinedResource | 要区分latency与启动间隔 | 固定输入下首结果延迟、稳态吞吐和背压均符合配置 |
| 待验 | X06 | 多消费者buffer引用 | 多任务共享输入/输出 | 最后消费者结束前不回收；异常与取消正确减引用 |
| 待验 | X07 | Stream/Credit/AddressRouter | 目标系统确实需要 | credit守恒、背压、包边界、路由错误各有针对性用例 |
| 待验 | X08 | RTL/ISS/详细DDR或NoC后端 | 具体精度或软件需求 | 单独定义对齐范围、参考数据、适配边界和判据后验收 |

历史建议曾将 X01–X04 优先于其他扩展；当前优先级以公共资产批次计划为准。X08不设置泛化的“误差≤5%”指标；有对应参考和使用范围后再制定，不能为完成计划虚构准确率。

### 8.1 多AXI端口SRAM性能专项：独立验收包

| 历史记录/待验 | ID | 任务 | 依赖与验收 |
|---|---|---|---|
| 待验 | X09 | multiport_sram_perf模型/配置/负载 | X01 + X07中的Router能力、已有Queue/Observe/Bench；复用而非复制资产 |
| 待验 | X10 | 事务源、拆分重组与响应容量 | 同ID同方向顺序、beat到16 B片段映射、有限outstanding/重组/返回队列；不要求pin-level AXI |
| 待验 | X11 | performance专项比较报告 | 执行示例第14节M0–M8，提供每端口/bank/Router/返回路径指标和证据 |

X09–X11是用户指定的Performance-Oriented ESL验收范围；不因基础包通过而自动通过，也不要求先实现X03寄存器、X04描述符或X08 RTL。X07可只完成本案例用到的Router，不必顺带实现无关Stream/Credit资产。

专项判据权威来源为示例第14.6节，执行清单如下：

| 历史记录/待验 | ID | 通过条件 |
|---|---|---|
| 待验 | MP01 | M0同bank8 ticks、异bank1 tick；完整128 B beat拆为8片段 |
| 待验 | MP02 | M1–M5无容量/资源上界违规；16×16 B/tick=256 B/tick为bank总服务上限 |
| 待验 | MP03 | M4公平容量预算下比较FIFO与VOQ，展示HOL等待与对照，不把增大buffer冒充策略收益 |
| 待验 | MP04 | M6暂停/恢复返回端时背压传播、无丢失/无限缓冲、最终outstanding为0 |
| 待验 | MP05 | M7受控条件下round-robin grant间隔满足最多7次其他端口grant |
| 待验 | MP06 | M8真实数据、strobe、拆分/重组、同ID顺序与完成可见性正确 |
| 待验 | MP07 | Skill选对performance profile，解释热点/交织/Router瓶颈，列出AXI抽象范围与未校准限制 |

该包完成需要X09–X11、MP01–MP07及U02的真实执行通过。traffic_only仍是可选；若实现，增加同一数据无关短负载与full_data的事务/时间一致性检查，并明确数值未验证。此处的资源上界不是RTL性能误差指标。



## 9. 验收结果怎样记录

不额外写厚报告。工具产出的检查记录和结果文件作为证据，补一页 `acceptance_summary.md` 即可；它属于 Repo 的测试/报告资产体系，不放入 Skill。

摘要至少包含：

| 内容 | 要求 |
|---|---|
| 对象 | Repo/Skill源码revision，必要时记录dirty内容hash，环境版本 |
| 范围 | 基础包 / 多端口SRAM性能专项包 / 其他按需扩展，各自列明 |
| 状态 | 通过、失败、阻塞、未运行项目及原因 |
| 证据 | run/checks/metrics/trace的相对路径或制品链接 |
| 结论 | 基础是否可用、哪些扩展可用、剩余限制 |
| 重现 | 一个环境入口和一组可执行命令 |

只引用既有输出，不把配置、指标抄入多份文档。发现问题时保留失败run，修复后产生新run，不能覆盖失败记录使其看起来从未发生。



## 13. 工作类型覆盖的补充TODO

保留历史 R/X/C 编号；方法测试所需 fixture 是 Repo 工程资产，方法的评分原则由 Suite 管理。

| 历史记录/待验 | ID | 资产TODO | 依赖与完成条件 |
|---|---|---|---|
| 待验 | R33 | 组合能力与兼容检查补齐 | 扩展R03/R04/R31；检查端口方向/合同、地址、profile能力、时间owner；错误到具体实例 |
| 待验 | R34 | 工作类型/独立调用测试fixture | common/testing或tests中提供现有/缺失/不兼容/planned候选和工具结果样例；Skill仅存方法 |
| 待验 | R35 | 负载自检与差分支撑 | 在R24基础上检查请求总量、范围、依赖、停止/排空；跨profile对齐数据和终态 |
| 待验 | X12 | 校准支撑资产 | 明确校准需求时启动；参考索引、适配、误差计算、拟合/验证区分与适用域；真实验收需要真实参考 |
| 待验 | X13 | 第三方后端导入与迁移 | 有实际外部模型时启动；来源/许可、依赖、语义映射、适配及消费者测试；基础仅需小fixture验证方法 |

基础新增资产验收：

| ID | 操作与通过条件 | 关联 |
|---|---|---|
| C01 | 合法系统通过；方向/类型/地址冲突及planned候选分别拒绝；错误指出实例和字段 | R33 |
| C02 | 同一物理内存重复owner或旧聚合时间与新内存时间并存，显式配置能被检查发现；不能假称静态证明任意模型内部代码 | R33 |
| C03 | 已有、缺失、不兼容三类fixture导出不同选择/缺口结果；mock结果始终标明 | R34/S18 |
| C04 | 故意错误的字节量、地址范围、依赖/结束条件被负载自检发现；同语义两profile数据/终态对齐 | R35 |
