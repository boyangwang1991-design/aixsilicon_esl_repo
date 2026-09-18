# SystemC 公共基础组件合同 v1

包 AixEslCommon 0.1，target aix::esl::common，命名空间 aix::esl。唯一实现位于 common/systemc/include/aix/esl；公开头文件为 bounded_queue.hpp、blocking_gate.hpp、byte_store.hpp、activity_monitor.hpp。Mmio32 保持内部 helper，不由此包导出。仅用于同一 SystemC 仿真内核上下文，不支持宿主线程并发调用，不是 RTL/HLS 综合接口。

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

构造时 counters=false 关闭全部统计，snapshot.enabled=false，其余字段为零；业务容量/数据/event/时间保持不变。每实例独立，无全局计数器。此处只交付计数和占用积分，不包含 trace、跨度重叠分析或文件输出；完整观察服务仍单独 planned。

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
  row, not a DRAM geometry claim. Regional maps remain model-specific for now.
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
- `InterruptState`: latched pending bits, enable mask, W1C and popcount threshold;
  timing and SystemC signal driving belong to the composing model.
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
allocation pages and 64-bit capacity. Timing/ROM permission are owner policies;
masked burst operations currently belong to ByteStore, not this minimal backend.

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
failure propagation. Merely querying an error/rate field is not evidence that every
model supports that perturbation.

The task-pipeline consumer composes two independent load/compute/store chains with
finite buffers and timed links. Load issues at 0/1 ns, arrives at 2/3 ns, compute
latency is 8 ns with II=1, and stores take 2 ns: last completion must be 13 ns.
The failure fixture cancels one branch without cancelling the independent chain.
This is a compute occupancy placeholder, with no numerical algorithm claim.
