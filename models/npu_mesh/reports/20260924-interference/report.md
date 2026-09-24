# Decode 干扰基线：结果与架构建议

2026-09-24。SystemC full_data、原生 SRAM + 显式 DDR；未做 RTL 校准。
本轮完成固定 Decode cohort 的独占/混合干扰分析及单变量诊断，未增加 QoS 仲裁机制。
精确源码以 [验证指纹](checks.json) 为准；12点实验与回归源码指纹一致。

## 主要结果

128个相同256B Decode读，source=3、SRAM router=0；周期4096开始，每64周期释放一次。
Prefill为1KB SRAM写，KV为4KB DDR→SRAM DMA；背景地址不与Decode重叠。
相同非零初始化、相同Decode ID/地址/依赖/释放时间。详细方法见
[实验合同](../../docs/interference.md)和[生效配置](baseline_config.json)。

| 场景 | Decode p95 | Decode p99 | 相对原配置独占p99 | 全批结束cycle |
|---|---:|---:|---:|---:|
| 独占 | 43 | 43 | 1.000× | 12269 |
| + Prefill | 48 | 48 | 1.116× | 14706 |
| + KV | 50 | 51 | 1.186× | 15853 |
| + Prefill + KV | 53 | 53 | 1.233× | 24032 |
| 混合，link 32→64B | 57 | 59 | 1.372× | 14893 |
| 混合，endpoint slots 16→32 | 53 | 53 | 1.233× | 24032 |
| 混合，fragment window 8→2 | 49 | 56 | 1.302× | 24200 |
| 混合，SRAM bank II 2→1 | 47 | 47 | 1.093× | 24029 |
| 混合，DDR 32→64B/cycle | 54 | 54 | 1.256× | 24030 |
| 混合，背景释放率减半 | 48 | 48 | 1.116× | 13713 |
| 混合，背景相位+17 | 51 | 51 | 1.186× | 24049 |
| 混合，背景相位+37 | 53 | 53 | 1.233× | 24037 |

![Decode p99](interference.svg)

资源点保持混合工作负载完全相同。减半背景点减少了任务数，不能据全批时间宣称配置加速；
相位点只改变背景释放时刻。所有比率分母为原配置独占43，并非各资源点自己配置下的干扰倍数。

## 等待在哪里

平均周期可加和，p99不可加和。以下为逐Decode单片trace统计：

| 场景 | 准入 | NIU发出前 | 请求传输 | 目标排队/服务 | 返回传输 | 总均值 |
|---|---:|---:|---:|---:|---:|---:|
| 独占 | 0 | 1 | 7 | 20 | 15 | 43 |
| 混合 | 0 | 1 | 7.500 | 21.867 | 15.000 | 45.367 |
| 混合，link=64B | 0 | 1 | 7.500 | 24.359 | 11.297 | 44.156 |
| 混合，bank II=1 | 0 | 1 | 7.500 | 18.359 | 15.000 | 41.859 |

混合流平均增量主要出现在目标侧。更宽链路降低返回传输时间，却增加目标侧排队/服务，
并未改善p99；该现象支持“上游加速改变竞争到达时序”的解释，但尚不能分离其每一种仲裁原因。
较快bank服务改善Decode尾延迟，却几乎不改变全批完成时间，说明总批次吞吐与Decode尾延迟
受不同因素限制。目标slots翻倍无变化，不支持优先扩大该队列的建议。

全run高阻塞端口集中在KV/Prefill写流路径，包括router4/output3/VN1和router5/output1/VN1。
Decode走source3至router0的短路径，未穷举源目的位置或热点映射；不能将本场景结论推广为
所有Mesh路径的干扰上界。端口计数和原生SRAM冲突计数见[机器可读结果](interference_checks.json)，
这些是跨资源事件计数，包含初始化和排空，不等同于Decode等待周期。

## 流量交付与下一步

下面是固定[4096,12288)窗口内成功完成字节/8192；KV只计目的有效字节，未将源读重复记为任务吞吐。

| 场景 | Decode B/cycle | Prefill B/cycle | KV B/cycle |
|---|---:|---:|---:|
| alone | 4.000 | 0.000 | 0.000 |
| prefill | 4.000 | 25.375 | 0.000 |
| kv | 4.000 | 0.000 | 23.500 |
| mixed | 4.000 | 14.000 | 13.500 |
| mixed_link64 | 4.000 | 25.250 | 25.000 |
| mixed_slots32 | 4.000 | 14.000 | 13.500 |
| mixed_fragment2 | 4.000 | 14.000 | 13.000 |
| mixed_bank_ii1 | 4.000 | 14.000 | 13.500 |
| mixed_ddr64 | 4.000 | 14.000 | 14.000 |
| mixed_background_half_rate | 4.000 | 14.000 | 13.500 |
| mixed_phase17 | 4.000 | 14.000 | 13.500 |
| mixed_phase37 | 4.000 | 14.000 | 13.500 |

所有点Decode均交付128次读、窗口吞吐4B/cycle；尾延迟差异不是Decode丢失或截断造成的。
背景开放释放时间表会受地址复用依赖及有限资源反压，实际注入不是无限流量。

**下一步优先验证 NIU 背景整形 + 目标端服务区分。** 半速背景实验提示整形可能改善尾部，
但它不是已实现token bucket的证据，也没有证明背景吞吐代价可接受。后续应保留完整背景任务集，
在NIU限制注入速率/突发，并把服务类别传递至Memory NIU/存储仲裁，比较Decode p99与背景交付率。
Router加权仲裁需随后单独对照，避免只改Router却忽视目标bank竞争。仍需多负载强度、相位、
位置、地址映射及更大样本，才能评估1.5×目标；本批12点不能视为QoS保证。

## 验证和可复现性

- 12点均通过真实字节oracle、资源界限、固定Decode cohort及trace阶段守恒检查。
- [44个SystemC测试](ctest.txt)、[42个Mesh Python测试](pytest.txt)、
  [85项CLI/布局/Mesh/SRAM Python兼容性检查](python_compatibility.txt)通过。
- 七类既有BM、源码消费者、搬迁安装消费者通过；[make check](workflow_check.txt)和
  [pre-commit](precommit.txt)通过。
- 修复回放器外层循环比模型周期领先一拍的问题。现在release判断统一使用模型周期，所有BM
  检查release<=accepted<=done。初次试跑`runs/mesh-interference-a`出现负准入等待，已作废，
  不用于本报告；最终数据来自`runs/mesh-interference-final`和`runs/mesh-interference-validation`。
- 用`esl npu-mesh interference --config models/npu_mesh/configs/sram_ddr.yaml --output <new-dir>`
  可重跑。源文件未提交，当前身份由SHA256确定；无发布或正式架构冻结。

统计为有限确定性样本，nearest-rank p99取128样本中的第127个，包含全部排空完成。
DDR仍为带宽/延迟/队列模型；没有DRAM命令、真实推理、PPA或RTL误差量化。
