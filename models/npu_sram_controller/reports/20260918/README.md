# NPU SRAM 性能模型：最终仿真与结果分析

本轮完成 **1045 次主扫描与 8 次仲裁对照，全部执行通过**。训练集选出的方案为 **B32_G32_xor0**：32 Bank × 32 B、32 B 交织、XOR shift=0、flat 网络、VOQ、RR、每端口 32 outstanding，容量 8 MiB；保持 C0 的队列与链路预算。配置见 [recommended.yaml](recommended.yaml)。这是有限候选集中的训练集优选，**未证明全局最优，也未达到整体 NPU 时长降低 10% 的目标**。

## 目标与结论

| 指标 | 目标 | 实际结果 | 判定 |
|---|---:|---:|---|
| 无冲突长流量有效读带宽 | ≥921.6 B/cycle，理论 1024 的 90% | 1019.02 B/cycle，约 99.51% | 达成 |
| NPU 八类负载等权几何平均时长 | 相对 C0 ≤0.90 | 训练 0.968282；保留集 0.996820 | 未达成 |
| 保留集最坏单负载退化 | ≤10% | 1.0204%，embedding seed 149 | 达成 |
| 推荐方案 buffer 容量代理 | ≤C0 的 1.25 倍 | 1.00 倍，3,866,624 bits | 达成 |
| 仿真执行与守恒检查 | 无失败点 | 主扫描 1045/1045；仲裁对照 8/8 | 通过 |

“时长降低”按 `1−候选时长/基线时长` 计算，不混用吞吐提升百分比。长流量测量包括启动与排空，是完整窗口吞吐；不是剔除预热后的稳态测量。`summary.json` 的 PASS 表示执行通过，不能解释为全部性能目标达成。

保留集综合收益仅 **0.318%**，不足以据此承诺普遍性能收益。随机 embedding 的五组配对 seed 时长比 95% 区间为 **[0.97554, 1.00889]**，包含 1。建议将 XOR 作为可配置映射方案保留；在没有 RTL 时序/面积校准和实际工作负载之前，不据此硬化为唯一映射。

## Benchmark 与搜索边界

主扫描包括 140 组 Bank/交织配置 × 3 个微基准 = 420 次，48 个架构 × 12 个训练实例 = 576 次，4 个冻结候选 × 12 个保留实例 = 48 次，以及 1 次长流量峰值检查。训练与保留集均有 GEMM、低并行 GEMM、prefill、decode、卷积、转置、DMA 并发和 embedding 八类；embedding 各用五个 seed，先在类内聚合，避免随机类因实例多而获得额外权重。

所有 NPU 流量通过实际存储完成反馈释放 DAG 依赖，包含有限 ping-pong buffer 生命周期和 compute/load/store 重叠。训练与保留集变更 shape、布局/stride 和随机 seed；详细形状、tensor 地址、字节总量与计算假设见 [benchmark.json](benchmark.json)。没有执行数学 GEMM/Attention 数值计算，不能把流量守恒说成 NPU 数值正确。

主寻优空间包括 8/16/32/64 Bank 等总带宽组织，16–2048 B 七档交织粒度，modulo 与四档 XOR shift；再比较 flat/hierarchical、远端链路 64/128/256 B/cycle、FIFO/VOQ、outstanding、group/local XOR、region、返回带宽与 ECC/双端口对照。不是这些参数的完整笛卡尔积。综合选型只在无 ECC、1RW 家族内进行，约束训练最坏退化 ≤10%、buffer ≤1.25 倍；[selection_frozen.json](selection_frozen.json) 在保留集运行前写入，保留集没有重新调参。

## 推荐方案逐类表现

下表为候选时长/C0，越低越好。C0 为 32 Bank × 32 B、32 B modulo 交织、flat/VOQ/RR。

| 负载 | 训练 | 保留集 |
|---|---:|---:|
| GEMM | 0.846507 | 1.000130 |
| GEMM tail | 0.955607 | 1.000000 |
| Prefill | 0.999686 | 1.001857 |
| Decode | 1.000000 | 1.000000 |
| Convolution | 1.006608 | 1.000549 |
| Transpose | 0.973154 | 0.979798 |
| Concurrent DMA | 0.972590 | 1.000359 |
| Embedding，五 seed 几何平均 | 1.002923 | 0.992072 |

训练 GEMM 的时长从 **3922 降至 3320 cycles（−15.35%）**，p99 从 384 降至 68 cycles，DAG 依赖等待累计量从 6153 降至 1343 tile-cycles。A/B 行步长 1024 B 恰好等于 32×32 B 的 modulo 周期；XOR 将更高地址位混入 Bank 选择，改变行间分布。结合地址映射图和等待指标，收益主要表现为分派/前端压力下降。

不能用 Bank 仲裁冲突计数单独判定好坏：这个 GEMM 中 C0 的 Bank 冲突为 0，而 XOR 为 3049；C0 请求已经在 Bank 之前受限，XOR 允许更多请求抵达 Bank。前端失败尝试数从 972431 降至 4432，但这是**尝试次数，不是周期数**；也不能将 DAG 依赖等待等同纯 memory stall。

保留 GEMM 改为 192×192×128、leading dimension 520，A/B 行步长 1040 B，自身打破原有 modulo 同余规律，且 tile 分配与计算关键路径改变：C0/XOR 分别为 **7714/7715 cycles**。虽然 p99 从 225 降至 198，算子时长没有改善。这是“改善访存局部指标不保证改善 NPU 总时长”的实际反例；shape 与 layout 同时改变，因此本轮不能将收益消失全部归因于 padding。

## 其他架构与仲裁结论

- 合成微基准优选 B8_G2048_xor1，在 NPU 训练集反而平均慢 **3.13%**，最坏慢 **26.67%**；transpose 慢 24.16%，embedding 类慢 24.51%。不能只用连续带宽微基准选架构。
- 同一粗粒度组织下，分层远端宽度 64/128/256 的训练时长比分别为 **1.251806/1.129782/1.101737**；加宽能够缓解限制，但多跳与布局不匹配仍有代价。
- 返回宽度降至 32 B/cycle 后，综合时长比 **1.178538**，prefill 达 **1.280044**。因此不能仅按 Bank 总带宽估算系统性能。
- B64_G16_xor0 训练比 0.972940，但 buffer 代理 4,947,968 bits，约 C0 的 **1.2797 倍**，超过预算；增加 Bank 不能当成免费收益。B16_G64_xor1 更省 buffer，但保留集综合比 1.001920，未优于 C0。
- 同 ECC 家族对照：ECC_B8_G2048_xor1 相对 C_ECC 综合慢 **2.713%**，最坏慢 **25%**，不推荐。ECC 报告与无 ECC 选型分开，不能跨可靠性等级宣称优胜。
- 四种仲裁在 concurrent 上的时长：RR **13608**、age **13624**、固定 2:1 weighted **13610**、read-first **13608 cycles**；embedding seed 11 均 **134 cycles**。当前两个负载没有显示换仲裁带来整体收益，也不能由此推出所有热点下公平性等价。见 [arbitration.json](arbitration.json)。

模型能处理同 Bank 访问竞争、有限队列反压、FIFO HOL、同 ID 同方向顺序、独立 AW/W 与 RMW 冲突。14 个 SystemC 用例覆盖映射双射、独立数据 oracle、精确 latency/II、返回反压、ECC、层次网络、保序、非法请求、reset、并发部分写、双端口、HOL 与 W-before-AW；源码/安装/搬迁三个独立消费者通过，Python 回归 47 项通过。RR 在本 NPU 流量中完成不等于任意场景的形式化公平性证明。

## 图表、证据与复现

打开 [index.html](index.html) 查看 23 张离线图表与可搜索结果表，包括交织扫描、实际地址到 Bank 分布、Bank 服务热力图、事务时间线、NPU load/compute/store 时间线、延迟 CDF 和资源/性能散点图。PNG/SVG 均保留；Bank/事务图来自有上限 trace，表示标题说明的窗口，颜色是窗口内服务次数，不是整个运行的 Bank 利用率。

精简归档保留 [results.csv](results.csv)、[ranking.json](ranking.json)、[selected_metrics.json](selected_metrics.json)、[checks.json](checks.json)、[candidates.yaml](candidates.yaml) 与配置/源码哈希。按用户要求，原始 trace、每次构建/安装树、旧试跑及逐次运行目录已清理；JSON/CSV 中原 run_dir 为历史标识，不再是可访问目录。生成器与固定 seed 保留在模型配套工具中，可重新生成相同流量和全量证据。

从 workflow 根使用 [模型 README](../../README.md) 中的 CLI，把新的 `--output` 指向本模型 `reports/<new-run>`。需要 SystemC 3.0.2、C++17/CMake、现有 uv 根环境及 PyYAML/Matplotlib。最终归档调整仅排除 reports 参与源码哈希并更新入口文档，未改变主扫描使用的 SystemC 实现或工作负载；独立验证在调整后重新执行。原始扫描哈希和当前验证哈希分别保留，不回填或伪造历史证据。

当前模型是未经过 RTL/实测校准的资源性能模型，direct AXI channel events 而非 pin-level VIP；每 ID 队头展开偏保守，ECC 为故障/资源抽象，本地 PE 端口争用、任意 WRR 表、多时钟等未覆盖。详见 [设计边界](../../docs/design.md)。buffer/switch 为容量与交换规模代理，不能代替综合面积、布线成本或频率。
