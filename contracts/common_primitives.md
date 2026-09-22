# SystemC 公共基础组件合同 v1

包 AixEslCommon 0.1，target aix::esl::common，命名空间 aix::esl。唯一实现按职责位于 primitives/infrastructure/adapters/services/workloads/verification 下的 systemc/include/aix/esl；common 只保留聚合构建与文档入口；公开头文件清单以 common/common.yaml 为准，下文逐项限定能力。Mmio32 保持内部 helper，不由此包导出。仅用于同一 SystemC 仿真内核上下文，不支持宿主线程并发调用，不是 RTL/HLS 综合接口。

## 容量、credit 与组合守恒

BoundedQueue、BlockingGate、ResourceTiming 统一提供 `capacity()` 与 `available()`，
查询无副作用，不依赖统计开关。Queue 满足 `capacity = size + available`；
Gate/ResourceTiming 满足 `capacity = outstanding + available`，单位均为记录/票据，非字节。
`available` 只表示容量余量，不承诺准入：关闭的 Gate 仍可有余量，ResourceTiming
可能受 II 限制。调用方必须检查 enter/reserve/try_push 的实际结果，拒绝后保留原请求。

Queue 的 credit 在 pop/clear 时归还；Gate 在 leave 时归还；ResourceTiming 在合法
retire 时归还。服务时间到期不释放票据，输出已进入完成 FIFO 也不释放票据。
输入、资源、输出容量独立配置；ResourceTiming 不隐含另一个无限输出队列。
owner 应持有全部在途 payload，输出 FIFO 满时保留已完成结果，直到消费者实际取走。
串接服务与输出 FIFO 时，输出项引用对应票据，不能再把两者相加当作独立在途事务。

若 Gate 覆盖从进入输入队列到最终消费的全生命周期，则每次完成状态转换后必须满足：

```text
已接受输入 - 已消费结果 - 已终结取消 = Gate.outstanding
输入队列.size + 在途记录数 = Gate.outstanding
在途记录数 = ResourceTiming.outstanding
输出队列.size = 在途记录中已入输出队列的数量
```

上述等式对应一请求一票据的组合，不扩展为任意拆分/合并协议。clear 不终结业务任务，
不得单独清空队列却遗留 Gate credit；取消必须由 owner 逐项终结和释放。
Queue/Gate 统计启用且未溢出时还有 `accepted = removed + discarded + level`；
拒绝不计 accepted，Gate 的 discarded 恒为零。关闭统计时仍用业务状态验证容量守恒。
high_watermark 是历史峰值，非水位触发事件；任意门限通知仍由 owner 定义。

独立 `credit_pipeline` 消费者使用输入深度 2、全程 credit 3、资源容量 2、latency=3 ns、
II=1 ns、输出深度 1/2。下游先暂停至 8 ns，再每 2 ns 消费一个结果，六个请求的
服务准入时刻必须为 0/1/8/10/12/14 ns，消费时刻为 8/10/12/14/16/18 ns。
输出深度只改变暂存背压，不能提前归还服务 credit。统计 on/off 使用相同独立预期。

## BoundedQueue<T>

固定正容量、复制型 FIFO；try_push(const T&) 成功返回 true，满返回 false 且不复制/移动源对象、不丢旧数据。try_pop(T&) 空时返回 false 且保持输出值；复制/赋值异常不移除队首。front() 空时抛 out_of_range，pop() 空时返回 false。clear 丢弃全部存储项并记 discarded，不产生业务 Completion；上层需自行逐任务终结。T 必须支持复制，复制/赋值不能自行 wait，异常时输出对象的状态取决于 T 的赋值保证。

容量只统计当前存储项，不私自计入已经取出的在服役项。DMA command 队列保存等待命令，完成队列保存未取结果；它们的总 credit 仍由 DMA 业务层控制。UART 保留正在发送的 TX 队首直到交付，因此该队列占用自然包含该帧。组件不替上层决定容量口径或 reset 策略。

changed_event 在成功 push/pop/非空 clear 后以 SC_ZERO_TIME 通知；同一 delta 的多个变化可合并，事件不是计数器。等待者必须先查 full/empty 等谓词再 wait，醒来再次检查；无等待者时事件不保留。组件本身不 wait，不插入目标时延，不提供仲裁或自动重试。

## BlockingGate / BlockingLease

Gate 正容量统计 enter 成功而尚未 leave 的 credit，可覆盖等待和在服务请求。满/关闭时 enter=false，无排队/自动重试。drain 关闭准入但不取消现有持有者，idle 表示 outstanding=0；仅 idle 时 resume 成功。最后一个 leave 通过 delta 通知 idle_event；已 idle 时调用 drain 不保证新事件，先查谓词。重复/无匹配 leave 抛 logic_error，计数不下溢。

推荐 BlockingLease(gate)：构造申请一个 credit，bool 转换报告是否持有，析构自动释放；release 可重复调用，不重复释放。租约不可复制/移动，gate 必须比租约存活更久。不能先 enter 再构造租约，也不能手动 leave 掉租约所有的 credit。原内部 adopt 语义在首次公开前已迁移，host/bus/MMIO 的调用一起更新。

Gate 不是服务引擎，不决定仲裁、执行时长、epoch 或取消。reset 正在使用的 buffer/payload 必须由上层终结后释放，不能用清零计数假装取消。该限制避免把不同模型的生命周期错误地合成一个基类。

## ByteStore

正容量私有 byte vector，初始映像不可超长，其余字节置零；clear 全部清零，不自动恢复初始映像。无时间、仲裁、payload 保留或隐式共享 owner。

read/write 校验整个请求后逐字节复制；拒绝空指针、零长度、越界/地址溢出、非法 byte-enable，返回 false 且不改目标存储/输出。byte-enable 为空时 length 必须为零；非空时长度为正，元素只能 00/FF，并按长度循环。disabled 写不修改存储，disabled 读不修改输出字节。调用方保证缓冲区足够长、有效且不与 store 自身内存别名；不提供重叠 memcpy/memmove 语义。contains 是纯区间查询，允许 size 位置的零长区间；实际 read/write 不接受零长度。operator[] 带边界检查，只用于明确单字节初始化/访问。

RAM/ROM 保留 TLM 错误优先级、时序、reset epoch 和只读策略，在服务完成时调用 ByteStore；存储本身不应该决定这些模型行为。

## ActivityMonitor / ActivityStats

Queue/Gate 共享被动统计：accepted、removed、rejected、discarded、当前 level、high_watermark、occupancy_ticks、elapsed_ticks。计数含义由容器状态转换决定；队列空 pop 不算入队拒绝，clear 计 discarded；Gate enter 满/关闭计 rejected、leave 计 removed。

时间单位是 SystemC 最小分辨率 tick，occupancy_ticks 为从构造起的半开时间区间上 level 的积分；snapshot 包含最后一次变化到当前时刻的尾区间，且不改变状态或仿真时间。平均占用可用 occupancy_ticks/elapsed_ticks（分母非零），不是利用率/吞吐硬件校准值。乘加和计数溢出饱和到 uint64 最大值并置 overflow，不通过统计异常改变模型运行；overflow 结果不能继续当作精确量使用。

构造时 counters=false 关闭全部统计，snapshot.enabled=false，其余字段为零；业务容量/数据/event/时间保持不变。每实例独立，无全局计数器。此处只交付计数和占用积分，不包含 trace、跨度重叠分析或文件输出；通用聚合指标见下节，观测事件仍由 EventRecorder 记录。

## Counter / Gauge / Span

`statistics.hpp` 提供同内核、无 wait/事件通知、常量内存的被动聚合 API。
所有时间字段为 SystemC 分辨率的整数 tick，snapshot 不推进时间或修改状态。
构造参数 enabled=false 时所有数值输出为零；控制模型的容量/调度不能依赖指标值。
计数/积分/总时长发生 uint64 溢出时饱和并置 overflow；溢出结果不能作精确性能结论。
ActivityMonitor 与新指标共用饱和加法规则。

- `Counter(enabled).add(n)`：单调非负计数；snapshot 为 enabled/overflow/value。
- `Gauge(initial, enabled).set(level)`：非负分段常量；snapshot 为 enabled/overflow、
  level/minimum/maximum、integral_ticks/busy_ticks/elapsed_ticks。
  积分覆盖 [构造时刻, snapshot)，包括最后一次 set 后的尾区间；busy_ticks 仅积 level>0
  的时间，不把 level=2 解释为两倍忙时。minimum/maximum 含初值和同刻 set 的样本。
  平均占用为 integral/elapsed，忙碌比例为 busy/elapsed；elapsed=0 时均无定义。
- `Span(enabled).record(begin,end)`：汇总已完成的区间；snapshot 为 enabled/overflow、
  count/total_ticks/minimum_ticks/maximum_ticks。end<begin 或 end>当前时间总是拒绝，
  off 模式同样校验；合法零长区间计一个样本。空样本 min/max 均为零，以 count 区分。
  重叠区间分别累加，不是忙时并集。调用方保证一次完成只 record 一次；组件不保存 ID、
  不跟踪未完成 span，也不生成虚假的完整延迟分位数。需要关联 ID 时使用 EventRecorder。

吞吐以明确窗口内 Counter 值/窗口长度计算，不预置单位转换或忽略排空尾部。
独立 fixture 的 level=1/2/1 持续 2/3/4 ns 得到积分 12 ns、忙时 9 ns；
重叠 span [0,5]、[2,9] 的总时长同为 12 ns，但忙时并集为 9 ns。
第二消费者 credit_pipeline 的六个完成 span 总时长为 33 ns，活动服务积分为 33 ns、
忙时为 18 ns；统计 on/off 保持相同数据与准入/消费时刻。

## Architecture primitives (SystemC)

The installed `aix::esl::common` target also exports these independent facilities:

- `ResourceTiming`: passive SystemC-time reservation ledger. Latency and initiation
  interval are independent; replicas have separate issue slots. Capacity includes
  completed-but-unconsumed tickets. `retire` rejects early, duplicate and unknown
  completions; reset requires drain. The owner schedules SystemC events and owns
  data visibility. No internal scheduler or unbounded completion queue is implied.
- `AddressMapper`: contiguous, striped and XOR-striped bijections over a finite
  capacity, with bank/group/local/stripe-row coordinates and inverse. XOR requires
  power-of-two banks. Group is contiguous physical bank grouping; row means stripe
  row, not a DRAM geometry claim. Regional windows are provided by RegionMapper below.
- `Arbiter`: committing RR, weighted RR and fixed-index priority grants, optional
  starvation age guard. Invoke only when the destination can accept. Weights count
  grants, not bytes; readiness must be sampled at each arbitration opportunity.
- `Transaction`: owned bytes and cyclic 00/FF mask; metadata carries source, QoS,
  unique tag, parent, epoch and issued SystemC tick. `TransactionBinding` is a scoped
  synchronous TLM adapter; it restores the previous metadata extension, but other
  payload fields retain the binding and must not outlive the transaction. It is not
  an AXI pin-level or nonblocking TLM implementation.
- `split_transaction` and `CompletionAssembly`: bounded-width boundary splitting,
  original byte offsets and exactly-once fragment completion/error aggregation.
  The caller carries data/masks by those offsets and owns response storage.
- `OrderedCompletion`: finite credits retained until retirement, unique live tags,
  ordered retirement within each stream and independent progress across streams.
  `barrier_ready` means all results were consumed, not merely produced.
- `DeterministicRng`: stable SplitMix64 with named FNV-1a streams and unbiased bounded
  sampling. Its snapshot is RNG state only, not a SystemC checkpoint.
- `RegisterBank`: descriptor-driven aligned 32-bit registers, RW/RO/W1C masks,
  byte enables, reset, hardware update and dump. Post-write callbacks see committed
  state; callback exceptions do not roll back the write. Descriptor generation from
  the cross-repository register SSOT is a separate integration task.
- `InterruptState`: latched pending bits, enable mask, W1C and popcount threshold.
  Constructor `edge_mask` selects rising-edge inputs; other inputs are sampled
  active-high levels. `sample(levels, clear_bits)` gives new events priority over
  same-sample W1C. A held edge does not retrigger; a held level relatches on the
  next sample. Reset clears sampled history, so the next high edge input is new.
  Timing and SystemC signal driving belong to the composing model.
- `Secded64`: extended Hamming 64+8 functional SECDED, exhaustive one/two-bit fault
  fixture. Three-or-more-bit errors are outside the guarantee. ECC resource timing,
  RMW atomicity and scrub policy are separate composition responsibilities.
- `TaskGraph`: deterministic ID-order closed-loop readiness, finite running slots
  and output buffers, failure cancellation, cycle rejection. A producer buffer
  survives until its last direct consumer terminates. Leaf buffers are released
  at completion. The owner executes SystemC work and explicitly reports outcomes;
  impossible buffer progress is reported by `stalled`, never silently overcommitted.

The architecture and service fixtures exercise these APIs independently of DMA,
UART and SRAM. They are preliminary independent evidence, not proof that all
planned integrations, adapters, checkpoint recovery or RTL calibration are done.

## Channels, observation and lifecycle

`TimedChannel<T>` models store-and-forward delivery at
`accept + ceil(bytes/width)*period + latency`. Serialization controls subsequent
acceptance independently of pipeline latency. Entries remain charged through
consumption plus credit-return delay. Owners call `receive` at SystemC wakeups;
there is no hidden process or competing timebase. Reset requires drained credits.
Use distinct instances for request and response paths. Routing and topology policy
remain at the composition layer.

`ClockDomainQueue<T>` exposes an item at the first destination edge at or after
acceptance, plus the configured synchronizer cycles. Edges are multiples of the
destination period from time zero. Reset explicitly discards entries and returns
the discard count; the owner must terminate affected requests. Physical CDC and
metastability are outside this transaction-level contract.

`SparseStore` provides zero-filled, byte-addressed sparse storage with 4096-byte
allocation pages and 64-bit capacity. Its initialization, size/contains, burst
read/write, cyclic byte-enable and clear API matches `ByteStore`; both use the
same `storage_access_valid` contract. Disabled read lanes preserve the output
buffer; disabled writes do not allocate pages. Invalid requests cannot change data.
Sparse writes allocate all needed pages before modifying bytes; allocation failure
may leave zero-filled pages but no partial byte updates. Buffers must not alias
backing storage. Timing and ROM permissions remain owner policies.

`SimulationLifecycle` enforces initialize/warmup/measure/drain/finished. The
measurement window ends when injection stops; drain remains functionally checked.
`ProgressWatchdog` retains named wait reasons and deadlines; declared future service
completion suppresses false stalls. It is a diagnostic primitive, not a proof of
absence of deadlock; polling and system queue snapshots belong to the owner.

`TrafficSource` produces deterministic sequential/stride/random/hotspot aligned
transactions and configurable read/write mix. Owners hold rejected requests and
limit outstanding work; generating another request on retry loses semantics.
`EventRecorder` offers off/counters/trace, bounded trace storage, complete event
counts despite trace drops, explicit dropped count and SystemC tick units. CSV v1
uses restricted ASCII identifiers; this is an observation trace, not a replayable
memory workload. `tools/common_explore.py` strictly analyzes the composition subset
(accept/service/complete/retire); it does not silently interpret arbitrary events.
Its bank heat reports average active service transactions, not busy utilization.

## Verification and perturbation

`MemoryScoreboard` is an independent vector-backed oracle. Feed committed writes
in the declared data-visibility order and check reads at their sampling point;
checking in arbitrary response order is invalid when addresses overlap. It honors
cyclic byte masks and identifies the first mismatch by transaction/source/address.
It does not call ByteStore or the address mapper to derive its expected bytes.
`ConservationChecker` enforces live-tag uniqueness, finite admission, exact completion
bytes and drained termination. `BandwidthChecker` enforces bytes per fixed, half-open
SystemC-time window; it is not a sliding-window limiter or a serialization model.

`FaultSchedule` defines explicit half-open target-specific pause/error/delay/rate
windows; overlapping windows for one target are rejected rather than guessing a
combination rule. Querying is side-effect-free. Composing models must document the
application boundary and implement the effect. The common Bank fixture applies a
pause at admission and demonstrates recovery; the task fixture demonstrates branch
failure propagation. FaultService below applies the supported single-issue effects; other owners must
explicitly opt in. Querying an error/rate field alone does not apply it.

The task-pipeline consumer composes two independent load/compute/store chains with
finite buffers and timed links. Load issues at 0/1 ns, arrives at 2/3 ns, compute
latency is 8 ns with II=1, and stores take 2 ns: last completion must be 13 ns.
The failure fixture cancels one branch without cancelling the independent chain.
This is a compute occupancy placeholder, with no numerical algorithm claim.

## ECC storage and compute timing

`EccMemory<Storage>` composes the shared storage API with actual 64+8 SECDED. The
capacity is a positive multiple of 8; word indices address little-endian 64-bit
codewords. Parity storage is independent, initially zero, and fault injection can
flip any of the 72 data/check bits. It supports either ByteStore or SparseStore.

- Full writes replace data and ECC without reading the old word, including a bad
  old codeword. Zero byte masks are no-ops.
- Partial writes read/decode once, merge enabled bytes, encode/write once. A
  correctable old word is corrected before merging. An uncorrectable old word
  aborts the write without changing its stored bytes or check bits.
- Reads return corrected data but do not repair physical storage. Uncorrectable
  reads return status plus zero data, never an apparently valid poisoned value.
- `scrub_word` reads/decodes one word and repairs only corrected words; clean and
  uncorrectable words are never rewritten. Owners choose the address sequence,
  interval and arbitration with demand traffic.
- Each result carries the exact read/write/decode/encode operation counts; service
  statistics also count corrections, uncorrectable observations, RMWs and repairs.
  Repeated observations of the same fault count repeatedly until repaired.

These are functional services, with no hidden wait or global scheduler. The
SystemC owner must serialize a complete multi-stage RMW/scrub sequence for a word
and schedule visibility at completion. The storage reliability consumer shares a
2 ns bank and 1 ns codec between scrub and demand: a corrected scrub occupies
read/decode/encode/write for 6 ns; demand arriving at 1 ns finishes at 12 ns,
versus 7 ns without the competing scrub. Both dense and sparse backends preserve
the demand update. This example is serialized per word; it does not claim a
multi-bank ECC scheduler, RTL-calibrated latency or physical protection coverage.
Allocation exceptions are infrastructure failures, not modeled ECC errors.

`ComputeTiming` converts positive work units to latency using
`ceil(work_units / units_per_cycle) + pipeline_cycles`, then multiplies by the
SystemC clock period with overflow checks. Initiation interval is independently
`initiation_cycles * period`. `resource(work_units)` creates a finite
`ResourceTiming` for homogeneous jobs of that size; variable-size jobs require
separate owner scheduling, not silently changing an active resource. Work units
are caller-defined (MACs/bytes/elements); no numerical algorithm or PPA is implied.

`WorkloadTrace` reads/writes versioned, bounded request plans with owned transaction
bytes, cyclic masks, integer earliest cycles and topologically ordered dependencies.
Its period must match the consumer clock; invalid or forward references are rejected.
The consumer defines retirement and preserves requests under rejection. This is
separate from EventRecorder's bounded observation stream. See the multibank contract
for the v1 record layout and its supported metadata subset.

## RegionMapper 区域表

`region_mapper.hpp` 复用 AddressMapper 的分区/交织/XOR 数学映射。构造参数为逻辑地址空间
容量、物理 Bank 数、每 Bank 容量、物理 group 数和非空 Region 列表；实现只保存映射表，
不分配存储、不推进目标时间、不产生请求重组或地址路由时延。
Region 包含 base/length/local_base、有序物理 banks、stripe、policy、xor_shift、rotation。
每区域先按相对地址映射到 bank 子集，再按 rotation 循环移位，最后查 banks 列表得到物理 Bank。
XOR 子集大小须为 2 的幂；分区/普通交织允许任意正子集大小。length 须能被子集大小×stripe 整除。
base/local_base 不要求全局 stripe 对齐；对齐和 ECC/协议限制由消费者另外检查。

逻辑与物理窗口均为半开区间。构造拒绝逻辑重叠、同物理 Bank 的窗口重叠、重复/越界 Bank、
容量越界、零长度、非法策略/shift/rotation 和加法溢出；相邻窗口以及不同 Bank 的同 local 窗口合法。
表的输入顺序不影响布线，但 map 返回的 region 索引保持输入顺序。允许逻辑和物理地址空洞，
map/inverse 遇到空洞或范围外地址均抛 out_of_range，不回退到默认映射。

map 返回 region/bank/group/local/row/offset：group 是物理 Bank 的等分组编号，local 是
Bank 绝对字节地址，row/offset 是减去区域 local_base 后的 stripe 行号/行内字节偏移。
inverse(bank,local) 返回唯一逻辑字节地址。映射按字节定义，跨区域请求的拆分、权限与事务完成
仍归 owner；没有新增完整 AXI 协议或运行时重映射能力。

公共 fixture 使用固定 Bank 布线表独立核对每个已映射字节，检查所有逻辑/物理空洞、
64-bit 上界、dense/sparse 存储的 mask 和未使用窗口。NPU 区域模式已复用此组件，
保留该模型原有最多 8 区域、2 的幂子集、modulo/xor、word/ECC 对齐等更严格配置边界；
其普通 group_first/local_xor 映射仍由 NPU 专属策略负责。

## BurstSchedule 有限突发计划

`burst_schedule.hpp` 定义普通事务的时间批次，复用 TrafficSource 的顺序/stride/随机/热点
地址模式与读写混合，不定义 AXI beat/last 或跨 Bank 拆分。构造参数为 requests、
burst_requests、period_cycles、phase_cycles；前两项必须为正，最后一批允许不足 burst_requests。
第 i 个请求的最早释放周期是 `phase + floor(i / burst_requests) * period`。
period 是批次起始间隔，非上一批实际完成后的空闲时间；period=0 表示全部同刻释放。
调度无内部时钟/事件/随机状态，earliest 查询无副作用；周期域由 owner 定义。

size 给出确切请求数，total_bytes(width) 校验并返回总字节量；越界索引、零宽度、
最终释放周期或总字节量溢出明确拒绝。owner 必须先判断最早周期、保留拒绝的同一请求，
只在成功准入后前进；停止注入以 size 为准，完成仍需排空所有在途事务。
请求可以跨多个批次积压，但不会因重试改变释放计划、重新抽样地址/读写或添加请求。

独立闭环 fixture：5 个 4-byte 请求，每批 2 个、周期 5、相位 2，释放时刻
2/2/7/7/12；单 credit、3 ns 服务给出准入 2/5/8/11/14 ns、完成 5/8/11/14/17 ns，
精确 20 bytes 并排空。随机混合流量与无背压生成的同 seed 计划一致；trace 往返保留批次时刻。
第二消费者 multibank 直接录制此计划，真实资源背压与 WorkloadTrace 回放负责实际时序。

## FaultService 与可重放随机窗口

`fault_service.hpp` 是单发射资源适配器，复用 ResourceTiming，并保存私有 FaultSchedule 副本。
构造传入 base latency/II、总在途容量、schedule 和非空 target；每个 Bank/链路可有独立实例。
其 reserve/retire/capacity/available/outstanding/reset 与公共 credit 口径一致。
Ticket 只含 id/ready/error；payload、实际读写与错误响应仍由 owner 执行。适配器无内核进程、
无隐式 wait/自动重试，不拥有用户事务；多副本资源需 owner 组合多个实例。

所有效果在**成功准入的目标时刻**采样：

- pause 或 bandwidth_percent=0：拒绝新准入，保留原请求；已有票据继续完成/消费。
- extra_latency：ready=accept+base_latency+extra_latency，故障窗口结束也不缩短已接收票据。
- error：在票据中锁存失败标记；owner 在 ready 后返回失败，不执行成功写入或提交成功状态。
- 1..100% 带宽：下一次准入不得早于 accept+ceil(base_II_ticks*100/percent)。按整数 tick
  向上取整；已预留的间隔跨越窗口终点仍有效。此处是固定宽度请求的准入速率缩减，
  不声称任意可变长度流量的字节带宽整形；base latency 与 II 仍独立。

延迟/间隔的计算溢出在申请 credit 前拒绝。结果就绪不释放容量，只有消费后的合法 retire
归还；未知/重复/提前（含额外延迟期间）退休和活动 reset 均拒绝。reset 需排空，保留
票据 ID 单调性，不平移既有绝对时间窗。错误也必须被明确消费，不能靠清空计数消除。

FaultSchedule 按 target/begin 排序导出 windows()；同 target 窗口不重叠，不同 target
可重叠。seeded(target,count,slot,duration,seed,stream,effect) 在每个从时间零开始的 slot
内，以固定 SplitMix64/FNV-1a 子流选择一个均匀整数 tick 偏移，生成固定 duration 的窗口。
要求正 slot/duration、duration<=slot，count=0 生成空表，拒绝时间范围溢出。窗口在构造时
一次性生成，at 查询不消耗 RNG；可保存实际 windows 表并重新构造，不依赖查询/线程顺序。

独立消费者使用 8 个固定 4-byte 请求、latency=2 ns、II=1 ns、容量 2：基线/纯错误在
9 ns 排空，暂停/零带宽/50% 准入速率场景在 12 ns，额外 4 ns 延迟场景在 13 ns，
组合错误+延迟+限速场景在 14 ns。固定 oracle 检查每个准入时刻、失败 ID、全部存储字节、
目标隔离和 credit 守恒；组合观测 on/off 结果一致。另有提前退休、延迟输出消费、
溢出原子性以及 seed=7/stream=fault.bank0 的固定整数窗口 5/9/20/26 tick 重放检查。
原 common_composition 已接入此适配器应用 Bank 暂停；multibank 配置型系统尚未开放故障
配置，不由这些公共测试推断其支持全部效果，也不声称物理故障/可靠性校准已完成。
