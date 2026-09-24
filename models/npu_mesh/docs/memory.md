# 存储接入与并发配置

`configs/sram_ddr.yaml` 给出 DDR+SRAM 混合系统。默认未指定 backend 时保留 DDR 服务模型，
已有配置仍可运行；明确 backend=sram 才会实例化实际 npu_sram_controller。

| 参数 | 语义 |
|---|---|
| fragment_window（1..64） | 单个 NIU 事务已发出但未返回的最大片段数；每周期最多发一片 |
| dma_window（1..64） | 单 DMA job 同时保留的源数据片数；每片 <= max_transfer |
| return_bytes | 每 source 的完整 read 预留总量，包括未消费 completion，不是可透支 ROB |
| endpoint.slots | head 接收预留总量：组包+等待+服务+等待响应注入，含服务中项 |
| endpoint.backend | ddr / sram；未知值拒绝 |
| DDR bytes_per_cycle | 读写共享服务通道的序列化速率 |
| DDR latency | read_latency/write_latency 为0时的默认固定延迟 |
| DDR read_latency / write_latency | 非零时覆盖 latency；与序列化独立，可流水重叠 |
| DDR turnaround_cycles | 前后请求方向不同时，服务通道另占的切换周期 |
| endpoint.sram | 原 SRAM Config 字段；banks/stripe/mapping/bank_ii/dual_port/队列等实际生效 |

SRAM effective capacity 来自 endpoint.size，full_data 固定为1；C++ config() 返回这些有效值。
CLI 显式指定不同 capacity 或 full_data=0 会拒绝。SRAM 不使用 endpoint 的 DDR 时延/速率字段，
其成本完全由 sram 子配置决定。C++ Config::validate 与 SRAM 自身 validate 共同检查合法性。

## SRAM 数据适配

Mesh 分片在接收端转为128B对齐 beat（最多32拍、不跨4KB），边缘 WRITE 以原 byte-enable
和边界 mask 保护邻接字节，READ 去除填充。padding 访问实际占用 SRAM bank/返回资源，
在 target-N.json 的原生 read_bytes/bank_services 中可见，不收费为额外 NoC payload。
当前适配要求 endpoint base 按4KB对齐，size 按128B对齐；SRAM region-holes 和 scrub 暂不接入。
这是一种明确的 Normal-memory adapter profile，不用于 Device 访问次数语义。

source 以 `source % sram.ports` 映射至原生端口，port 数上限8由 SRAM 模型规定；多个 source
映射同端口时会真实共享资源。原生 ID 由 packet 编号对 ids 取模，使用返回 token 唯一追踪，
不会用 ID 当唯一身份。AW 接收顺序决定 W 发送顺序，push_w/pop 各自遵守原生每拍限制。
端点槽在 SRAM 内部阻塞、返回等待时保持占用，反压可以一路传回 Router。

initialize/inspect 仅用于 idle full_data 预置/检查，通过 SRAM Mapper 访问同一存储，
没有影子存储，不驱动性能时钟。Target::stall 对 SRAM 阻止新 native 提交，但已接受写继续
提供 W 和取走响应；对 DDR 暂停未完成服务。注错 corrupt_next 在提交 native 前将下一片
标为 DATA_CORRUPT；不是实际 SRAM ECC bit 注错模型。

## 完成、失败、复位

成功片段经过目标可见观察点后记录 completed_ranges；所有已发片段终结前不发布父完成。
首错停止继续注入；已经送至 SRAM 的请求无法撤销，可能继续提交写，必须记录部分效果。
TIMEOUT_UNCERTAIN 的时间阈值意味着开始停止/排空，不代表硬性有界返回延迟。

Mesh reset 不调用 SRAM reset（后者会清存储且仅支持 quiescent）。它清空未提交数据并保留
已被 SRAM 接受的请求，继续送 W/收 R/B，随后发 abort；Model::idle 还等待原生内部 credit 排空。
系统必须等待 Model、TensorDma 和 AXI frontend 均排空；frontend 自身在途 reset 仍不支持。
DMA 捕获总线 epoch，变化后取消，不会在 resume 后悄悄继续旧任务。

## 集成与运行

npu_mesh CMake 导出依赖 `AixEslNpuSramController`，源码构建复用已存在的 target 或自动
add_subdirectory；安装时两个包共同安装，搬迁消费者通过 find_dependency 找到 SRAM。
外部消费者 fixture 实际运行 SRAM endpoint，而不是仅验证链接成功。

`esl npu-mesh memory-explore --config <sram_ddr.yaml> --output <new-dir>` 默认运行
四个8KB、双目的大组播任务及写初始化/读回：NIU窗口1/2/4/8 × DMA窗口1/2/4，另比较
SRAM banks4/8/16、dual_port、目标slots=2和DDR方向切换开销。完整结果包含每点配置、
逐目标原生指标、源读取字节、片段/返回预留峰值与 oracle。无 RTL 校准，不作绝对芯片预测。

原生 SRAM 若报告写错误，完成结果标记 `uncertain`：`bytes`/`completed_ranges` 仅统计确认成功的片段，不将失败片段内可能发生的部分写入宣称为精确已知。
