# DMA 命令与生命周期

Config：capacity 默认 4（1..65535），burst_bytes 默认 64，max_transfer_bytes 默认 1048576；后两者为正 unsigned，且 burst_bytes <= max_transfer_bytes。构造时检查，不能在运行中改配置。

Command 为 `{id, source, destination, bytes}`，前三者 uint64_t、长度 unsigned。地址为字节地址，允许非对齐和尾块；长度必须为 1..max_transfer_bytes，源/目标区间不能溢出 uint64 或重叠（包括原地 copy）。首版不处理虚拟地址、cache coherent、descriptor 或寄存器前端。

submit 为非阻塞调用，返回 accepted/closed/full/invalid/duplicate。拒绝不创建完成记录。capacity 是总 credit 数：排队 + 在执行 + 尚未 pop 的完成记录；ID 在归还 credit 前不可重用。每次 accepted 恰好生成一条 Completion，pop_completion 才归还 credit，因此环境不取结果不会产生无限积压。

执行器逐命令 FIFO，每块先 LT read 再 LT write，只有一个在途 memory 事务；末块长度取剩余字节。只申请一个 burst 的内部数据缓冲，payload 存活到 b_transport 返回及 annotated delay 消费完成。所有读写共用一个 initiator，没有隐藏的第二条资源路径。下游负责路由/服务时间，DMA 消费返回 delay 一次，不叠加额外总带宽公式。

完成记录包含 ID、completed/failed/cancelled、bytes_written 和最后一次 TLM response。bytes_written 仅统计成功返回的写块；完成所有块才报告 completed。遇到任何非 OK（包括容量拒绝）停止命令并报告 failed，不隐式重试。已完成块不会撤回；目标应保证错误写无副作用，否则 bytes_written 只能表示已确认的成功字节量。INCOMPLETE_RESPONSE 用于未发出事务即被取消的排队命令。

request_drain 关闭新命令，已接收命令继续执行。idle 指没有排队或执行任务，不要求软件取完完成记录；空闲后可 resume，未取结果仍占容量。完成/idle 事件仅是唤醒提示，先查谓词/尝试 pop 再 wait，避免丢事件。

reset 关闭准入并提升 epoch：排队任务立即生成 cancelled；正在执行的 b_transport 不能强制中断，必须等它返回及 annotated delay 结束，期间保持 payload/buffer 有效，之后不发下一事务。若复位发生在读阶段，不再写本块；若在写阶段且下游写成功，统计该块并报告 cancelled。取消完成顺序可能是排队命令先于活动命令，必须按 ID 匹配；既有完成记录保留。resume 只在 idle 后成功。reset 是显式方法调用，不是持续保持的信号电平。

若下游永久阻塞，reset/drain 也无法承诺有限完成；系统必须提供有界目标响应。reset DMA 不会 reset host/bus/RAM，整个系统复位须按依赖顺序排空。这里的块级可见性不等于整命令原子性，软件仅在 completed 后才能使用完整目标区间；搬运期间其他主机不要改写该任务源/目标，否则读取的是每块服务时实际数据。
