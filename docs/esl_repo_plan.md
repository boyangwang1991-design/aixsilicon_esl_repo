---
document_type: repository-development-plan
name: esl_repo
document_version: 1.3.0
status: proposed
date: 2026-09-17
method_owner: esl-development-suite
example: esl_testcase.md
---

# esl_repo：面向架构探索的模型与公共组件库

本文替代上一版规划。核心调整：**公共组件做扎实，单次探索流程做轻；先能组合、能运行、能观察，再按问题增加精度。** 不要求每个模型建立完整需求文档、RTM、多级 Gate 或 RTL 校准。

本文是待实施方案。组件名、API、命令和数值示例均是拟开发合同，不表示已有可运行实现。配套文件：本仓 [测试案例](esl_testcase.md) 和 [资产进度](esl_todo.md)；开发方法唯一归 esl-development-suite。

## 1. 最小目标与开发边界

用户应能复用 DMA、BMU、SRAM、计算模型，修改几个参数就比较不同架构；能看到任务等待在哪里、带宽由谁占用、buffer 为什么不足，以及某个优化是否真的有效。

默认工作方式：写清架构问题 → 组合或补模型 → 运行 → 比较。每次只保留三类检查：配置合法、结果正确、性能统计自洽。需要固件兼容、RTL 对标或团队正式发布时，再开启对应检查。

| 默认需要 | 按需增加 | 本次不作为基础设施目标 |
|---|---|---|
| 有限队列/资源、数据与事件语义、最小 oracle、性能观测、可重跑配置 | 寄存器兼容、真实 descriptor ABI、AT 精细握手、RTL/ISS、校准 | 全套 CPU/DDR/NoC 自研、数据库、云调度、复杂权限平台 |
| model.yaml、SystemC 库、设计/集成/验证文档与消费者用例 | 完整需求文档、RTM、兼容矩阵、发布审批 | 为每个模型重复写多份规格或强制走多级 Gate |

探索结论可以基于假设。报告列明假设即可；缺少 RTL 不能阻止架构探索，但不应声称预测准确到某个百分比。

## 2. 技术分工与模型精度

- Python：可选算法行为参考/oracle；也可用于工程配置、测试驱动和结果分析，不能替代 SystemC 目标模型。
- C++ / SystemC：ESL 模型主体，包括行为、系统并发、事件、资源竞争和目标仿真时间。
- TLM：内存访问的互操作接口；命令、事件和 buffer 服务使用简单有类型接口。
- RTL/ISS：有具体需求时通过适配器加入，不是跑通基础示例的前提。

SystemC/TLM 的官方资料见 [Accellera](https://www.accellera.org/downloads/standards/systemc)。具体版本由工程环境固定；不在本规划中要求跟随最新版本。

Linux 作为统一构建/运行环境；Windows 用户可连接 Linux 环境。开发交付一个已验证环境配方和 `esl doctor`，检查编译器、CMake、Python、SystemC 与版本组合。容器是可选封装，不把容器平台变成额外门槛。

### 2.1 用途类别：可以组合，不是精度等级

| model_kinds | 中文 / 英文 | 主要内容 | 不自动意味着 |
|---|---|---|---|
| behavioral | 功能/行为模型 Functional / Behavioral | 算法、数据、状态、命令与异常 | 有可信的时间预测 |
| performance | 性能导向 Performance-Oriented | 容量、竞争、吞吐、延迟、背压 | 必须省略真实数据或达到周期精确 |
| software_visible | 软件可见 Software-Visible | 寄存器、descriptor、IRQ、软件可见副作用 | 具备完整CPU/OS或精确硬件性能 |
| microarchitecture | 微架构详细 Microarchitecture-Detailed | 指定内部资源、流水、路由/流控 | 整个模型逐周期等价RTL |

功能参考是 behavioral 的一种用途，不单独增加一种模型类别。多个类别可属于同一资产；复用行为核心，组合时间策略与前端，避免复制四份完整模型。纯性能模型允许没有真实数据路径；可选 Python 算法参考不依赖 SystemC，但其通过不代表目标模型交付。

### 2.2 profile：一组已经实现且经过声明的配置

统一字段如下；由Repo拥有Schema，其他文档只引用：

| 字段 | 取值/规则 |
|---|---|
| model_kinds | 上表类别的集合；标记资产用途，不承诺所有profile具备所有能力 |
| profiles | 命名配置的映射；每个profile显式列出其能力，不再使用简单字符串列表 |
| timing_model | untimed / annotated / resource_contention / cycle_accurate |
| data_modes | profile支持的full_data / traffic_only集合；运行时选一个data_mode |
| interface_mode | command / register / descriptor等项目前端名称 |
| transport | direct / LT / AT；异构端口时改由ports逐端口声明 |
| capabilities | 该profile实际支持的能力ID，如data_transform、shared_dma_contention |
| limitations | 未建模因素；可放README短说明并在报告引用 |

`functional/resource/detailed`保留为常见profile命名习惯，名称本身没有推断规则；具体项目可命名为pipeline_analytic、memory_resource。LT/AT描述传输方式，不是用途或精度。

只声明已实现的组合，选择不支持的profile/前端/数据模式必须拒绝。系统run保存逐实例解析后的profile与能力；“系统支持bank分析”要求相关路径具备bank模型，不能因为存在一个详细模型就给整个系统升级精度。

性能参数在README注明来源：assumed（假设）、analytic（解析）或calibrated（有参考数据），可按参数/指标分别标记；校准项加引用及适用范围。默认假设/解析即可探索，不新增校准Gate。

### 2.3 共用核心与建模边界

行为核心负责状态和功能，时间策略负责资源与延迟，接口前端负责命令/寄存器/descriptor转换；观察器接收统一事实。它们是组合边界，不要求一开始建立复杂插件框架。

纯功能模型不输出目标吞吐；performance模型使用full_data时能同时检查数据，使用traffic_only时只检查流量、依赖与资源。输入数据影响分支、压缩率、访问地址或任务数时，必须有足够的替代语义，才能省略真实计算。没有该语义就拒绝traffic_only，不能默认两种模式等价。

同一功能的跨profile对照比较数据与可观察事件语义；只有明确约定时才比较精确时刻。microarchitecture的cycle_accurate声明必须列明范围、时钟与参考；不强求所有模型都达到此级别。

## 3. 仓库结构与职责

实际目录唯一见 [仓库布局与迁移索引](repository_layout.md)。未来能力按 registry/TODO 推进，不在这里重复维护第二份目录树。

`common` 存可编译组件，`templates` 存少量薄骨架，`models` 存可实例化模型，`examples` 存具体组合。模板不得复制 common 的实现；修一次公共组件应让所有模型受益。

Skill 本体放既有 Skill 仓库，仅承载方法。ESL 专用生成器、构建/运行入口、观测器、测试工具及模板作为工程资产放本 Repo；可以调用既有 tool-repo 的通用能力，但不要求为 ESL 再拆一个工具仓库。

## 4. 公共组件：必须先讲清楚的能力

以下为头脑风暴后的收敛清单。A=基础示例需要；B=随第二批模型加入；C=有实际需求再做。优先级表示依赖和收益，不表示已实现。

| 组件 | 提供什么 | 必须约定的语义 | 优先级 |
|---|---|---|---|
| ModelBase | configure/reset/run/drain、实例 ID | reset 的 epoch、排空与关闭 | A |
| Config + Factory | 参数校验、实例化、最终配置输出 | 未知参数/非法连接报错；静态DAG环拒绝，不静默忽略 | A |
| MemoryPort | read/write 请求与完成 | 数据可见性、错误、byte enable、payload 生命周期 | A |
| CommandPort | submit/accepted/completed/failed | 拒绝或重试不算已接受；完成仅一次 | A |
| EventChannel | task/buffer/error 通知 | 关联 ID、epoch、作用域；事件历史不能被错过 | A |
| BoundedQueue | 有限队列、push/pop、等待与计数 | 深度是否含服务中项；拒绝不丢请求 | A |
| ServiceResource | 非流水服务时间与并行容量 | 请求接受、排队、开始、结束、释放 | A |
| Arbiter | 基础FIFO及示例store优先；round-robin等按需 | 相同时刻稳定排序；声明是否可能饥饿 | A/B |
| ByteStore | 真实内存、初始化、边界、byte enable | 地址空间唯一 owner | A |
| BankedMemory | 地址映射、bank 端口、冲突、服务队列 | bank-local 地址与事务拆分 | B |
| BufferPool | 固定块/slot 分配、handle、占用跟踪 | 原子分配、generation、防提前释放 | A |
| TaskGraph | 简单 DAG、依赖、状态 | 上游失败传播；不默认支持复杂抢占 | A |
| ComputeShell | 取数、功能 callback、资源预约、写回 | 数据何时读取/写入；取消如何排空 | A |
| RegBank | RW/RO/W1C 等寄存器语义 | strobe、对齐、保留位、副作用、复位 | B |
| IrqState | pending/mask/clear、聚合输出 | 电平重算；set/clear 同时发生的优先级 | B |
| DescriptorCodec | 明确字节布局的解析/生成 | endian、长度、保留位、非法字段 | B |
| PipelinedResource | 启动间隔、延迟、lane、输出背压 | 吞吐与 latency 分开定义 | B |
| CreditPool / StreamPort | 有限 credit、流式包 | credit 归还、包边界、取消清理 | B |
| AddressRouter | 地址窗与目标选择 | 重叠拒绝、访问错误、属性传递 | B |
| TraceReplay | 从记录产生请求 | open-loop/closed-loop 的适用区别 | B |
| RTL/ISS adapter | 更高精度或软件执行 | 同步、内存 owner、协议转换 | C |
| Detailed DRAM/NoC adapter | 引入已有详细后端 | 配置对齐、外部依赖、时间责任 | C |

公共组件不追求面面俱到。例如 BufferPool 首先支持固定 slot，不一开始实现通用堆、压缩、虚拟内存和复杂 eviction。

### 4.1 Queue、资源与事件的共同合同

所有异步操作至少携带 `request_id / task_id / epoch` 中适用的字段。请求有 accepted、started、completed/failed/cancelled 三类关键边界。

- Queue 的 `depth` 默认只计等待项；服务中项另计。模型需要总容量时显式配置，不让同名参数有不同含义。
- 请求被接受后必须最终终结；retry/rejected 请求由上游保留，不得同时进入内部队列。
- 完成事件先更新可见状态，再通知消费者；支持先检查状态再等待，避免丢唤醒。
- Payload/数据 buffer 的所有权跨异步调用保持有效。TLM 适配层负责协议与引用生命周期。
- 复位提升 epoch；旧事务终结前不回收其仍使用的存储，旧完成不得改变新任务。
- 服务资源区分非流水和流水；不能用单个 `busy_until` 假装实现所有流水结构。

仲裁仅有一个决策owner：Scheduler判断依赖/资源资格，资源端从已接受的等待集合选下一项；不能在Scheduler和Resource分别重排并得出不同顺序。基础FIFO与store优先是可选策略，不把BoundedQueue实现绑定死为FIFO服务顺序。

基础接口最少包含：MemoryRequest的地址空间、地址、长度、读写、可选数据/byte enable、request_id/epoch；Command的task_id、操作及类型化参数；Completion的原始ID、epoch、状态和错误原因。byte enable省略表示全部有效；非空时基础合同要求与请求长度一致，不隐式循环。MemoryPort读数据在服务完成时取样，写数据在完成时可见；请求期间写payload必须保持有效。存在同址并发访问时遵循资源服务顺序，其他ordering保证显式声明。

地址空间默认物理、非缓存、非一致性；local SRAM与外存通过address_space区分。基础copy拒绝源/目标重叠，拒绝越界/整数溢出与零长度；需要memmove/零长度成功语义时另列能力，不随实现偶然行为变化。基础系统采用单时钟域；多时钟、CDC、缓存一致性均为按需扩展。

### 4.2 内存公共组件如何拆

将内存拆为 **ByteStore（数据）+ AddressMap（地址）+ ServiceResource（时间）+ Policy（冲突/仲裁）**。

简单内存可以只用 ByteStore；SRAM 使用 bank/端口服务；聚合 DRAM 使用带宽与延迟服务。这样更换时间模型不会另造一份数据副本。

每个物理地址只有一个权威数据存储。DMA、CPU 代理、Compute 都通过约定接口访问；不能各持一份数组再在结束时对拷。

基础配置：容量、基地址、line_bytes、banks、ports_per_bank、read/write latency、共享或独立读写端口、queue depth、仲裁。地址映射示例：`bank = floor(offset / line_bytes) % banks`；跨 line/bank 的请求先拆分，不能按首地址给整笔请求归属一个 bank。

### 4.3 寄存器模板如何做

目标是复用访问语义，不为探索模型强制复刻完整寄存器表。

- 命令模式：直接提交高层命令，适合架构探索。
- 寄存器模式：加 RegBank + callback，调用同一个命令执行核心。
- 兼容模式：从已有权威寄存器规格导入，冻结真实地址/字段/副作用，服务驱动验证。

RegBank 负责偏移、宽度、reset value、访问权限、strobe；业务 callback 负责 doorbell、start、status side effect 等。W1C 仅对启用字节中的写 1 位清除；硬件 set 与软件 clear 的同刻优先级由字段契约指定。跨寄存器/非对齐访问基础模式拒绝，不隐式拆分。

模板不能默认任意寄存器写入都更新存储：RO、W1C、RC、WO 的行为需独立实现和测试。RC 等可列为可选能力，不支持时校验失败。

### 4.4 DMA、BMU、Scheduler 的合理建模深度

| 模型 | 默认实现 | 需要特定问题时再补 |
|---|---|---|
| DMA | 高层命令、多任务、有限队列、读写竞争、真实数据、完成/错误 | 真实 descriptor 取数/写回、寄存器兼容、AXI 特有排序 |
| BMU | 固定 buffer pool、slot/handle、等待/释放、容量统计 | 引用计数多消费者、分页、迁移、eviction |
| Scheduler | DAG 依赖、资源就绪、FIFO/优先级调度、失败传播 | 抢占、deadline、多 context 隔离 |
| Compute | 功能函数 + 延迟/吞吐 + 内存访问 | 内部 pipeline、局部 bank、算子详细阶段 |

descriptor 字段在影响地址、长度、stride、队列与完成语义时应保留；内部 FSM 和每个 RTL pipeline stage 无需复刻。若研究 descriptor fetch 开销，应真实产生内存访问；否则明确为零开销命令注入。

## 5. 性能测试与观察套件：esl-observe

`esl-observe` 是公共组件和工具能力集合，不是需要另装的一套重量级服务。默认本地文件输出；不要先建设数据库和网页后端。

### 5.1 四层结构

| 层 | 组件 | 作用 |
|---|---|---|
| 埋点 API | Counter / Gauge / Duration / TraceSpan | 模型只报告事实，不自己算一套 KPI |
| 收集层 | Collector / MetricRegistry / TraceSink | 统一时间、单位、tag、关联 ID、过滤 |
| 分析层 | Aggregator / RunComparator / BottleneckView | 计算吞吐、等待分解、占用、瓶颈候选 |
| 输出层 | JSON / CSV / Markdown / timeline export | 原始数据可重算，结果可比较与查看 |

### 5.2 最小 API 合同

以下为逻辑接口，不限定具体 C++ 命名：

```text
counter.add(metric_id, delta, bounded_labels)
gauge.set(metric_id, value, bounded_labels)
span.begin(kind, correlation_id, resource_id)
span.end(kind, correlation_id, status)
event.emit(kind, correlation_id, fields)
```

所有目标时间由仿真内核提供；wall time 单列。高基数 task/request ID 放 trace，不作为 Counter 的标签，避免几百万条任务生成几百万个指标。

Queue、ServiceResource、MemoryPort 等组件自带标准埋点；开发 DMA 无需重复实现 queue occupancy 统计。模型只补 `dma.bytes`、`bmu.alloc_wait` 等业务事实。

### 5.3 统一指标字典

| 指标 | 定义 | 关键注意事项 |
|---|---|---|
| task.latency | completion - first_submit | retry 不重置首次提交时间 |
| task.wait_dep/resource/queue | 互斥主状态的时间累计 | 多个同时阻塞原因另作重叠诊断，不能直接相加 |
| throughput | 窗口内完成的有效工作量 / 窗口时长 | 分清 samples、tasks、payload bytes |
| memory.bytes | 每条链路实际完成的读/写字节 | 不把内存两端字节重复算成用户吞吐 |
| resource.utilization | 占用积分 / (容量 × 窗口时长) | 多 lane 分母含容量，范围 0–1 |
| queue.mean_depth | 对深度随时间积分 / 窗口时长 | 不用 push/pop 次数平均替代时间平均 |
| queue.max_depth/full_time | 峰值与满队列时间 | 与等待项/服务项定义保持一致 |
| buffer.peak_bytes/wait_time | 峰值占用与分配等待 | 按实际分配块计，不只按有效数据计 |
| overlap_time | 两类执行区间交集的并集长度 | 不能累加所有 pair 的交集而重复计时 |
| errors/timeouts/cancelled | 分类计数 | 与 completed_success 分开 |
| simulator.wall_time | 宿主运行时间 | 不作为目标硬件性能 |

latency 给出 count、min、mean、P50/P95、max；固定 nearest-rank 口径并记录样本数。仅 1 个样本时不渲染成有统计代表性的分布。

分段统计窗口使用半开区间 `[start,end)`，资源积分按窗口裁剪。全程运行使用 `window_mode: whole_run`：在最终终结事件和同tick清理全部处理后采集，事件计数包含最终tick；积分仍按 `[start,final_tick)`，分母是实际elapsed ticks，不人为加一个tick。run.json保存窗口模式，避免漏掉最后一次完成或连续窗口双计。

latency仅统计提交和终结都属于窗口的任务，跨窗口任务另计。成功吞吐只计成功完成的有效工作；零时长/无样本用null与原因表示，不除零，也不输出伪零延迟/无限吞吐。小例子默认whole_run，无warm-up。

### 5.4 时间线与状态观察

基础时间线展示 Host、Scheduler、DMA、Compute、Memory 和 BMU：任务关联、依赖箭头、执行 span、队列与buffer曲线、错误标记。并发请求使用独立 lane 或合法 async 表达，不在一个同步轨道上输出交叉而非嵌套的 span。

内部保存整数 tick 与 time_resolution。可导出 Chrome Trace JSON，供 Perfetto 查看；导出器必须将 tick 换算为该格式的微秒单位，不仅修改显示标签。Perfetto 官方说明支持该格式的 slice、counter 和 flow；超大 trace 再考虑 native 格式。[Perfetto 外部格式文档](https://perfetto.dev/docs/getting-started/other-formats)

本地/受限环境保留离线查看路径，不默认上传 trace 到外部服务。

### 5.5 输出与开销控制

默认输出：

```text
runs/<run_id>/
  run.json                  # 最终配置、源码/工具 revision、seed、状态
  metrics.json              # 单位、窗口、结果、assumptions
  checks.json               # 哪些正确性/统计检查实际执行
  summary.md                # 一页结论、瓶颈候选、配置差异
  trace.jsonl               # 开启 trace 时才输出
  timeline.json             # 可选导出
```

提供 off / counters / trace 三档；默认 counters，失败时保留最近事件环形缓冲。trace 支持按实例、task、时间范围过滤、事件上限与 dropped_events 标记。完整指标独立于可能采样/截断的 trace，不从残缺 trace 计算“精确总量”。

off关闭性能采集与导出，但保留运行状态、最小正确性检查和最终仿真时刻；metrics标NOT_RUN，不伪造为零。失败环形缓冲默认仅在counters/trace启用；若关闭需在记录中说明。

观察代码不得推进仿真时间或改变仲裁结果；开关 trace 后功能与目标时序应一致。记录 trace 对宿主速度的开销即可。

### 5.6 瓶颈报告怎样避免过度解释

自动输出“候选瓶颈 + 支持证据”，例如“DMA busy 95%，Compute 等输入 40%”。再建议一次有针对性的参数对照：仅增加 DMA 带宽，观察总时长是否下降。

busy 高不必然是瓶颈；等待最多的模块也不必然是原因。结论要结合关键路径、吞吐变化与资源竞争。没有能耗/面积依据时不报告准确 PPA。

## 6. 性能测试公共设施：esl-bench

| 能力 | 要解决的问题 | 最小输出 |
|---|---|---|
| TrafficGenerator | 可控地址、长度、间隔、读写比例 | 请求集、seed、完成统计 |
| MicrobenchRunner | 独立测 queue/arbiter/memory/DMA | 解析预期与实际差异 |
| ScenarioRunner | 数据准备、命令注入、停止条件 | pass/fail/blocked 与重现配置 |
| Oracle/Scoreboard | 数据、事件、完成数、容量守恒 | 首个差异与关联 ID |
| FaultInjector | 指定第 N 次访问失败、延迟、取消/reset | 可重现故障与清理结果 |
| SweepRunner | 参数空间、可行域、逐点运行 | 每个点的状态与完整指标 |
| RunComparator | 比较同口径实验 | 配置 diff、性能差异、功能状态 |
| SanityChecker | 防止“数字好看但模型错误” | 带宽上界、容量、字节守恒、时间顺序 |

基础微基准覆盖顺序流、共享DMA读写竞争、队列与buffer容量；相关扩展实现后再提供其微基准。完整目录包括：无竞争顺序流、随机访问、同/异 bank、读写混合、单/多通道、queue depth sweep、producer-consumer 速度不匹配、buffer 容量 sweep。

区分 latency 模式（低注入率/单 outstanding）与 throughput 模式（足够请求并发）；不能拿排队延迟当空载 latency。仅在有已知参考时判定数值误差，否则作趋势和约束检查。

配置变化后保存所有点，包括非法配置、失败、超时；失败点不能变成耗时 0 或被无声删除。快速探索不要求统计学包装；有随机性时才增加重复 seed。

## 7. 模板体系：生成薄骨架，复用厚组件

### 7.1 建议模板目录

| 模板 | 内部复用组件 | 生成的薄骨架 | 自带测试 |
|---|---|---|---|
| functional | ModelBase、CommandPort | 功能 callback、model.yaml | 一个输入输出案例 |
| memory_target | ByteStore、MemoryPort、资源策略 | 地址/容量配置、响应 hook | 越界、byte enable、数据可见性 |
| banked_sram | BankedMemory、Arbiter | bank/端口映射配置 | 同 bank/异 bank 对照 |
| command_engine | Queue、ServiceResource、Event | accept/execute/complete | queue full、完成唯一性 |
| dma | command_engine、MemoryPort | copy/stride 核心 hook | 拷贝、竞争、访问错误 |
| buffer_manager | BufferPool、Event | 分配/释放策略 | 用尽、唤醒、重复释放 |
| register_device | RegBank、IrqState | 地址表引用和 callback | RW/RO/W1C、mask、reset |
| compute | ComputeShell | 数据类型、功能函数、时间参数 | 独立 oracle 和边界值 |
| pipeline_system | Factory、ScenarioRunner | system.yaml、oracle、README | 端到端最小样例 |
| parameter_sweep | SweepRunner、RunComparator | experiment.yaml | 非法点过滤与基线对照 |

模板只暴露可插入的配置、功能 hook、时间策略和业务埋点；不能留下一个“TODO 任意实现”却算可运行模板。生成后至少通过对应语言的构建/加载检查、执行一个有意义的小例子；尚未实现的能力明确关闭。

### 7.2 model.yaml 最小形态

```yaml
schema_version: 1
id: aixsilicon:esl:vector_affine:0.1.0
factory: vector_affine
model_kinds: [behavioral, performance]
profiles:
  functional:
    timing_model: untimed
    data_modes: [full_data]
    interface_mode: command
    transport: direct
    capabilities: [data_transform]
  resource:
    timing_model: resource_contention
    data_modes: [full_data]
    interface_mode: command
    transport: direct
    capabilities: [data_transform, finite_compute_service]
ports: {command: command.consumer, memory: memory.initiator, done: event.producer}
parameters:
  lanes: {type: integer, default: 8, min: 1}
  setup_ticks: {type: integer, default: 4, min: 0}
assumptions: [no_internal_cache]
# 示例为拟开发形态；实现前不能登记为available。
```

已有硬件规格的模型加 `hw_spec_ref / register_ref / descriptor_ref`，校准过的加 `calibration_ref`。不为未来用途预填几十个空字段。资产采用稳定 ID，源码用 Git revision；run.json 自动记录实际用到的版本，只有分发/复现实验时再导出独立 lock。

该示例的resource只声明计算资源，不承诺DMA或bank竞争。capabilities要与实际行为相符。候选模板/模型可在registry标planned，选择运行时只解析available资产；schema_version是格式版本，资产ID版本是行为版本，profile名字是配置选择，三者分开。

registry只维护ID、路径、类别与可用状态；详细能力读取model.yaml。依赖是单向的：contracts/common → models → examples；tools调用这些资产；templates引用common/models而不复制实现。生成器记录template版本，不覆盖已有用户文件；配置变更通过显式覆盖并保存最终配置。

### 7.3 四类用户需要写的内容

开发新模块原则上只需写：模型参数与端口；核心行为；特有时间/资源策略；少量测试和限制说明。构建骨架、计数器、trace、报告、随机种子、通用错误路径都尽量复用。

## 8. 配置、时间和接口的底线

1. system.yaml 保存实例和连接，experiment.yaml 保存场景/参数扫描；最终解析配置自动写进 run.json。
2. 通用事实采用 YAML；运行结果可用 JSON/CSV。不再额外建立 task/state/contract 等全套必填文件。
3. 一段延迟只由一个 owner 计费。聚合 DMA 延迟与详细内存服务互斥或明确拆分，禁止整段重复收费。
4. 目标时间用 SystemC 时间/整数 tick；cycles 指明时钟域，wall time 另列。
5. 完成必须对应规定的数据可见性。成功计数不能早于写入完成。
6. 队列/容量不能无穷大假装真实；使用无限资源作理想对照时须标注 ideal。
7. 未支持参数直接拒绝；TLM/LT/AT 或数据模式变化必须显式，不静默降低能力。
8. 不跨语言直接 memcpy C++ struct 作为 descriptor ABI；有硬件兼容要求时定义字节布局。
9. 同刻事件稳定排序；seed 可重放。统计不得依赖 hash map 的偶然遍历顺序。
10. 示例/测试失败就报告失败；没有运行就报告未运行。模型自洽不是硅片准确性证明。

## 9. 默认使用方式与轻量检查

当前可用入口（实际参数和范围以 [工具合同](../contracts/tool_contracts.md) 为准）：

```bash
esl inspect --kind model
esl validate --evidence
esl new model sample_register --template register_target
esl run reference/legacy_python/examples/mini_pipeline/system.yaml
esl sweep reference/legacy_python/examples/mini_pipeline/experiment.yaml
esl compare runs/baseline/result.json runs/double_buffer/result.json
```

`inspect`是轻量能力发现，返回模型/模板/profile、支持的参数、capabilities与可用状态，供CLI和Skill共同使用；不需要数据库。

当前顶层 `run` 只接受显式 legacy-python-mini-pipeline 后端，校验配置、独立数值 oracle 和服务时间界限。SystemC 已有 `multibank run/sweep` 固定拓扑配置入口（见 systems/multibank/README.md）和 `npu-sram` 专项入口；模型也可通过 CMake 消费者构建与验证。任意拓扑的通用 SystemC 配置装配仍是后续目标。

| 检查 | 何时执行 | 失败怎么处理 |
|---|---|---|
| 配置合法 | 每次运行前 | 明确指出字段/连接问题 |
| 结果正确 | 每次功能运行 | 保留失败输入与首个差异 |
| 性能自洽 | 每次性能运行 | 标记数据不可用于比较并定位原因 |

运行输出区分 `execution_status`（执行是否正常）、`checks[].status`（单项检查）和 `overall_status`（本次要求的检查是否满足）。任一必需检查FAIL则overall为FAIL；无FAIL但必需检查因环境受阻则BLOCKED；未执行则NOT_RUN。NOT_APPLICABLE仅由显式范围决定，不能消除用户要求。对纯功能模型，性能检查NOT_APPLICABLE；对合法traffic_only，数值检查NOT_APPLICABLE但流量/依赖检查仍需执行。

外层测试harness另有 `test_status`：注入故障或错误oracle后，若被测run按预期FAIL且诊断/清理正确，则该负向测试PASS。不能把被测run的FAIL覆盖成PASS。

校准、寄存器兼容、RTL 差分在相应任务才加入；不保留两套 G/R 编号。资源耗尽/异常测试重点覆盖公共组件和本次变更，不为修改 README 重跑全系统。

## 12. 支撑不同工作类型的资产组合

工作类型 W01–W14 及路由由 esl-development-suite 的 references/routing.md 定义。本节只规定工程资产，不在 Repo 重写 subskill 方法。所有新增能力仍为规划；模板可先以已有薄骨架组合，不能在 inspect 中提前标 available。

| 工作类型 | Repo 应提供的资产 | 用户项目通常只需补充 |
|---|---|---|
| W01 行为模型 | functional/command_engine、MemoryPort、ByteStore、事件、独立oracle harness | 业务功能/状态、边界和异常语义 |
| W02/W03 性能建模或增加profile | 有限Queue/Resource/Arbiter、PipelinedResource（按需）、Observe、解析microbench | 目标资源图、容量、竞争和时间策略 |
| W04/W05 系统组合/补缺 | 模型索引、Factory、端口合同、system模板、兼容校验、冒烟场景 | 实例/连接/地址、任务DAG、必要功能hook |
| W06 软件可见模型 | RegBank/IRQ、DescriptorCodec、前端适配与测试fixture | 权威寄存器/descriptor引用、业务副作用 |
| W07 微架构细化 | 有限credit、流水/路由策略、trace对齐和跨profile harness | 明确范围的结构/时序和参考 |
| W08 工作负载 | TrafficGenerator、TraceReplay、ScenarioRunner、oracle、负载自检 | 地址模式、请求分布、依赖、注入与停止条件 |
| W09 架构探索 | sweep/compare、参数约束、指标字典、报告模板 | 基线、参数域、目标和实验配置 |
| W10 验证/修复 | 可重放fixture、故障注入、等待图、差异定位、检查输出 | 最小复现与针对性回归 |
| W11 校准 | 参考数据索引、格式适配、误差计算与校准报告资产 | 来源、匹配配置/窗口、拟合/验证数据划分 |
| W12 资产维护 | registry、依赖/来源/许可记录、环境配方、打包/兼容检查 | 来源版本、本地改动、迁移说明 |
| W13 公共能力开发 | contracts/common/templates/tools及独立fixture | 新机制与至少一个消费者用例 |
| W14 文档规划 | 模型/系统README和报告薄模板、现有契约说明 | 本次范围、假设、待办和设计决定 |

不按14种工作类型创建14份模型或模板。同一个 DMA 核心可由不同前端和时间策略服务多个工作类型；同一组 Observe/Bench 资产服务模型单测和系统探索。

### 12.1 为组合与替换补齐资产合同

在第7节现有 model.yaml 的基础上，按实际需要增加说明或可校验字段。字段由 contracts 统一定义；不要求所有模型填无用空字段。

- **端口合同：**方向、事务类型/版本、地址空间、数据/字节使能语义、请求接受/重试、完成事件及ordering。异构端口逐个声明 transport，不以端口同名作为兼容证据。
- **资源合同：**谁拥有物理存储、谁收取延迟、资源共享组、容量口径、仲裁owner；组合时应能发现重复计时或复制同一内存。
- **运行合同：**时钟/单位、reset/epoch、drain/失败清理、payload生命周期；跨时钟或跨runtime必须有显式适配，不能直接连线冒充支持。
- **依赖合同：**资产ID、源码revision/内容hash、模板版本、必须能力；运行记录保存最终解析值，planned能力不能被装配为可运行资产。
- **替换合同：**功能保持范围、允许改变的时间/排序、需要重跑的测试及消费者。替换profile不要求绝对周期相等，但必须移除被替代的计时owner。

inspect 的结构化结果应足以回答“可用吗、能回答这个问题吗、可连接吗、缺什么”；不要求数据库。仅 registry 名称匹配不足以确认兼容。

### 12.2 新增公共资产的最小交付范围

| 资产 | 最小内容与检查 | 范围 |
|---|---|---|
| 组合兼容检查 | 方向/类型/版本、地址重叠、能力、时间单位与owner；错误定位到实例/连接 | 基础R03/R04/R31的补充 |
| 工作类型测试fixture | 已有/缺失/不兼容/planned模型的固定目录和期望事实；所有可执行内容留Repo | 方法测试基础；不与实际运行混称 |
| TrafficGenerator | 顺序、stride、热点、混合读写；按outstanding/背压闭环；固定seed与总量 | SRAM专项X09/X10 |
| workload自检 | 请求计数/字节、地址分布、依赖合法、停止/排空；不依赖DUT输出证明激励正确 | R24基础；性能负载随专项扩展 |
| 跨profile差分harness | 对齐任务/事务标识，比较数据/终态，时间比较仅按声明范围 | R24基础与X02扩展 |
| 校准支撑 | 参数来源索引、参考导入、误差计算，区分拟合/验证样本与零参考值 | 按需新增，不阻塞基础/SRAM专项 |
| 导入兼容fixture | 来源与license、缺依赖、接口不匹配、适配后运行的小型例子 | 维护方法先验；真实外部后端按需 |

### 12.3 DMA资产的可扩展组织

建议保持一个 models/dma 资产：行为核心实现copy/状态；时间策略实现aggregate或transaction资源；前端实现command及按需register/descriptor。公共Queue、Memory、Observe仍在common，不在DMA目录复制。

最小行为合同延用第4.1节：正长度、非重叠、合法范围、完成时数据可见。stride、多通道、SG、AXI特有能力须逐项声明；未声明不支持。单server聚合策略可回答基础双缓冲问题，不能用于证明读写通道并行或outstanding的收益。

纯行为任务不要求C++/SystemC；同一运行后端内优先复用核心。若Python参考与C++目标模型并存，通过独立oracle/差分保持一致，不要求为了共享源码引入复杂跨语言绑定。模型的可复用性以语义合同与测试为基础。
