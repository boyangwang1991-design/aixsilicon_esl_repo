# 文王 NPU Mesh ESL 0.1

SystemC 3.0.2/C++17 的实数据、有限资源总线子系统模型，依据
[总线契约](../../contracts/npu_mesh_axi_bus.md)。支持模块定位、字节级行为检查和
合成 benchmark 的相对架构比较；不代表契约全部 P0 已交付或文王整机已完成 ESL。

| 责任块 | 已实现 | 抽象/限制 |
|---|---|---|
| Router / link | XY、逐 flit、4 VN、每 VN 多 VC、输出 RR、延迟 credit、packet ownership、守恒检查 | 同步单时钟；不是 RTL 周期校准 |
| AXI NIU | 有界 AW/W/AR/B/R beat API、INCR、narrow/unaligned、mask、单目标同 ID 保序 | 非引脚 VIP；不支持 Device/FIXED/WRAP/exclusive/USER |
| Tile / memory NIU | token/poison、有限目标槽；真实 npu_sram_controller 接入；DDR 带宽/读写延迟/切换服务模型 | SRAM 对齐适配产生真实 padding 开销；DDR 非 DRAM command/PHY 模型 |
| NIU 分片 | fragment_window、完整读返回预留、按 offset 重组、失败后排空、部分可见范围 | 外部 AXI 同 ID 仍保守顺序；每事务每周期最多注入一片 |
| Tensor DMA | 1D/2D/SG、dma_window、逐片源复制、每目的进度、取消/复位后排空全部已发子事务 | 无 descriptor fetch、group table 或 router multicast |
| Command / sync | 独立每 Tile FIFO、代次/参与者去重、先到事件保留、失败传播 | command 是 opaque 消息；Sync 由消费者喂入完成，未实现完整 Scheduler |
| Management | 直接查询、trace、drain/resume/reset、空闲 map 更新、故障注入 | 非 CSR/IRQ 模型；全子系统 reset，不模拟单端 CDC reset |
| BM | 依赖回放、实数据读写/DMA、独立 Python oracle、参数扫描 | 合成流量，不是模型推理/TTFT 或真实芯片预测 |

实现、校验、时间责任见 [设计](docs/design.md)、[接入](docs/integration.md)、
[验证](docs/verification.md)。未覆盖项和后续顺序见 [差距](docs/gaps.md)。

在 workflow 根复用唯一 uv 环境（独立仓可安装本仓 Python 依赖）：

```bash
uv run --no-sync python repos/aixsilicon_esl_repo/tools/esl_cli.py npu-mesh validate --output repos/aixsilicon_esl_repo/runs/mesh-validation
uv run --no-sync python repos/aixsilicon_esl_repo/tools/esl_cli.py npu-mesh run --scenario mixed --output repos/aixsilicon_esl_repo/runs/mesh-bm
uv run --no-sync python repos/aixsilicon_esl_repo/tools/esl_cli.py npu-mesh explore --scenario mixed --output repos/aixsilicon_esl_repo/runs/mesh-sweep
uv run --no-sync python repos/aixsilicon_esl_repo/tools/esl_cli.py npu-mesh validate --config repos/aixsilicon_esl_repo/models/npu_mesh/configs/sram_ddr.yaml --output repos/aixsilicon_esl_repo/runs/mesh-memory-validation
uv run --no-sync python repos/aixsilicon_esl_repo/tools/esl_cli.py npu-mesh memory-explore --config repos/aixsilicon_esl_repo/models/npu_mesh/configs/sram_ddr.yaml --output repos/aixsilicon_esl_repo/runs/mesh-memory-sweep
```

输出路径必须不存在。使用 `--config` 指定 YAML，`--workload` 指定
`npu_mesh_bm/v1` JSON；省略 workload 则生成确定性流量。产物包括最终配置、输入、
completion/模块 trace、内存快照、metrics、独立 oracle 检查及源码 SHA256。
仿真失败保留日志并返回非零；未标定性能不能升级为 G2 架构冻结证据。

复用：ByteStore 直接引用公共实现；现有 dma 是 LT 连续 copy，不含 Mesh 分片/返回资源；
现有 tlm_bus 不含输入 VC/credit，所以新增本模型；SRAM 实例直接链接原库，不复制 bank 仲裁。
存储与并发参数、复位约束见 [存储接入](docs/memory.md)。

Decode独占/Prefill/KV混合干扰分析：见[固定输入与统计口径](docs/interference.md)。
`esl npu-mesh interference --config models/npu_mesh/configs/sram_ddr.yaml --output <new-dir>`
执行12个对照点并输出阶段时延、热点端口与p99图表。

[NIU令牌桶与SRAM入口QoS](docs/qos.md)可选启用；`esl npu-mesh qos-explore`使用完整相同背景任务集比较尾延迟/吞吐取舍。
