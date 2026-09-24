# NIU 整形与 SRAM 入口服务区分

本轮QoS仅控制NIU请求注入和SRAM原生请求提交，不改变Router RR或SRAM内部bank仲裁。
所有功能默认关闭，关闭时保留既有行为。context在Packet中保留，DMA子请求沿用父context。

| 参数 | 默认 | 行为 |
|---|---:|---|
| shape_bytes_per_cycle | 0 | 0关闭；否则每个(source,context)独立逻辑字节/cycle补充速率 |
| shape_burst_bytes | 4096 | 桶容量与初始token；启用时必须>=packet_bytes |
| shape_context_min | 2 | 只整形context>=此值；64表示没有被选中的context |
| SRAM endpoint.priority_context | 64 | 64关闭优先级；0..63为优先服务context |
| SRAM endpoint.age_cycles | 128 | 等待达到此阈值的请求优先于未老化的优先context |
| SRAM endpoint.issue_limit | 0 | 0沿用原生上限；非零限制该端点已被原生接受、尚未完成的请求数 |

## 资源与计时

令牌桶在首次遇到合格请求时满桶，按模型周期补充，上限封顶；只有Network::inject成功
才扣token。WRITE按有效请求长度收费（不扣除mask字节），READ按预期返回payload长度收费；
头部、padding与ACK不另收费。因此这是逻辑带宽整形，不是物理链路利用率限额。
DMA源读与目的写分别收费；不会只限制descriptor提交而漏掉子请求流量。
父任务已接受但token不足会保持所有权等待，不发布RETRY伪完成；异常、取消与超时沿用排空语义。
复位清桶，新epoch可重新获得burst；累计跨epoch计数不能用单epoch的burst上界解释。
shape_stalls为候选片段尝试次数而非互斥等待周期，shaped_bytes为成功注入收费总字节。

存储入口的等待队列仍受endpoint.slots覆盖，包括组包、等待、服务、响应注入等待；没有无界旁路。
每周期从已接收的waiting请求选择：老化请求按原始入口序号，随后优先context，随后其他请求；
同等级按入口序号。原生端口不能接收的候选允许跳过，不声称严格FIFO阻塞。
issue_limit只限制原生已接受的请求；端点队列总容量不变。已接受AW继续送W，R/B继续排空，
没有抢占、回滚或重复收费。老化是入口选择规则，不是在下游停顿下仍保证有界完成的证明。
这些参数不用于DDR（非默认priority/issue_limit拒绝）；DDR暂保留原有服务顺序。

## 实验

```bash
python tools/esl_cli.py npu-mesh qos-explore \
  --config models/npu_mesh/configs/sram_ddr.yaml --output runs/<new-dir>
```

独占Decode之外，全部11个混合点使用完全相同的完整工作负载及release/dependency：
128个Decode、288个Prefill写、72个KV搬运，初始化也相同。对比无QoS、FIFO/优先入口在途上限1和4、
8或16B/cycle整形、组合、burst256及age32。FIFO和优先策略对照必须保持issue_limit相同。
统计Decode p95/p99、背景在固定窗口内交付的字节、全部任务完成时间；整形不会删除背景任务。
窗口与分位数沿用[干扰实验定义](interference.md)。

所有点核对字节oracle、收费总字节和逐(source,context)token bucket上界；trace注入事件包含bytes。
新增微基准覆盖限速/豁免、复位、FIFO、优先级、老化覆盖优先级和原始到达顺序。
仍缺Router加权仲裁、bank内部服务类别传播、全局带宽配额、CSR动态配置和端到端确定性保证。
