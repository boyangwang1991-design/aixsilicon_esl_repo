# Decode 混合流干扰实验

运行入口：

```bash
python tools/esl_cli.py npu-mesh interference \
  --config models/npu_mesh/configs/sram_ddr.yaml --output runs/<new-dir>
```

实验固定128个256B Decode读，source=3/context=1，读取SRAM低4KB。释放周期从4096开始，
间隔64，共8192周期的释放窗口。相同初始化写入非零数据并在背景开始前完成；所有对照的
初始化和Decode行（ID、地址、数据、依赖、释放时刻）哈希完全相同。

背景从3072周期开始，持续释放到12288之前：Prefill为source=2的1KB SRAM写，间隔32；
KV为source=6的4KB DDR→SRAM DMA，间隔128。地址与Decode隔离，重复使用背景目的slot
有显式完成依赖；释放是开放时间表，实际接收仍受slot依赖和真实资源反压限制。
这些是总线访问替身，不执行NPU计算。释放率并不等于实际注入率。

12个实验点：独占、只加Prefill、只加KV、三类混合；在混合流上分别改link=64B、
endpoint slots=32、fragment_window=2、SRAM bank_ii=1、DDR带宽=64B/cycle；另有背景
释放率减半、背景整体偏移17和37周期。资源点保持混合输入不变，后3项明确改变背景输入。
参数以每点config.json为准。只支持两个按DDR/SRAM排序且至少64KB的端点，至少8个router，
4KB最大事务、>=256B packet和完整trace；不接受外部workload替换固定对照。

## 观测口径

- 全128个Decode样本计算release→done及accepted→done的count/min/mean/p50/p95/p99/max。
  nearest-rank分位数；p99取第127个样本。包含释放窗口之后的排空完成，无尾部截断。
- 窗口吞吐只计算[4096,12288)内完成的Decode字节/8192。另报告最后Decode完成时刻。
- 单片Decode trace：ACCEPTED→INJECTED_FRAGMENT→TRANSPORT_ACK→TARGET_VISIBLE→TASK_DONE。
  分别表示NIU发出前、请求传输、目标排队/服务、返回传输；准入等待另算release→accepted。
  请求/返回传输包含网络排队；目标项不能进一步宣称为精确bank排队或纯服务周期。
- 阶段均值可以相加，分位数不能相加。检查trace服务时长与completion一一统计守恒、
  事件唯一及时间单调，拒绝负准入时间。
- Router/output/VN stall和原生SRAM bank/frontend/ROB指标是全run聚合事件数，包含初始化、
  背景和排空；不能相加当作Decode等待周期，也不能单凭高计数断言瓶颈。
- 比率统一相对于原配置独占Decode；资源变化点不是各自配置的独占/混合比率。
  此实验不是稳态分布或QoS保证，不将1.5×阈值作为本轮功能验证通过条件。

回放器现在以Model周期判断release，消除外层循环领先模型一拍导致的负准入等待；
所有BM均检查release <= accepted <= done。早期历史报告的周期数保留，不用于新口径混算。

每点保留真实数据oracle检查、配置/输入/源码指纹、completions、trace、ports和target指标。
顶层checks.json记录固定cohort检查、阶段分布、热点端口和原生SRAM指标；CSV/SVG给出尾延迟对照。
