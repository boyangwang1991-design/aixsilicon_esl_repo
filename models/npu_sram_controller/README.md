# NPU SRAM Controller

可复用 SystemC 3.0.2 性能模型：8×1024-bit AXI 事件接口、有限 fragment 网络、可逆 Bank/region 交织、1RW/1R1W、ECC/RMW 和闭环 NPU DAG。Python 只生成地址/依赖、编排实验和离线报告；目标时间与资源竞争由 SystemC 执行。

本版本使用 direct channel API，不是 pin-level AXI VIP 或完整 TLM AXI bridge。完整范围和简化见 [设计](docs/design.md)、[验证](docs/verification.md)，公开库接入见 [集成](docs/integration.md)。架构提案列出的扩展不自动成为本版本能力。

从 workflow 根使用唯一 uv 环境：

```bash
UV_CACHE_DIR=/tmp/aix-uv-cache uv run --no-sync python repos/aixsilicon_esl_repo/tools/esl_cli.py npu-sram run --output repos/aixsilicon_esl_repo/runs/npu-single
UV_CACHE_DIR=/tmp/aix-uv-cache uv run --no-sync python repos/aixsilicon_esl_repo/tools/esl_cli.py npu-sram validate --output repos/aixsilicon_esl_repo/runs/npu-validation
UV_CACHE_DIR=/tmp/aix-uv-cache uv run --no-sync python repos/aixsilicon_esl_repo/tools/esl_cli.py npu-sram explore --output repos/aixsilicon_esl_repo/runs/npu-exploration
```

`run --config <architecture.yaml> --workload <trace.dag>` 可运行自定义配置/流量；省略时运行 C0 GEMM。`explore --quick` 仅做小搜索冒烟。SystemC 安装由 `SYSTEMC_HOME` 指定，模型配置不会下载依赖。建议每次使用新的 output 目录；验证消费者搬迁要求新目录。

输出：`index.html` 离线图表与可搜索结果表、`report.md`、`results.csv`、`ranking.json`、`ecc_ranking.json`、每个 run 的配置/源码/workload hash 与原始指标、有限窗口 trace。选择先在训练集冻结，再运行不同 shape/layout/seed 的保留集；ECC 与 none 分开排名。

复用决策：

| 现有资产 | 处理 |
|---|---|
| common ByteStore | 复用其独立字节存储 |
| RAM 模型 | 单服务资源无法表达此 Bank/AXI 竞争，保留不改 |
| planned banked_sram | 没有可复用实现，本模型独立交付 |
| ESLModel.cmake / environment contract | 复用 SystemC 版本锁、库导出和安装机制 |
| 历史 Python run/sweep | 不作为目标性能后端；新增 `esl npu-sram` 显式入口 |

所有性能结果是未校准资源模型中的相对比较，不是实际 SRAM macro 面积/频率或 NPU 芯片承诺。

最终交付见 [中文结果分析](reports/20260918/README.md) 和 [离线图表](reports/20260918/index.html)。报告、图表、推荐配置与精简验证证据统一归档到本模型的 `reports/`，本次临时构建、原始 trace 和试跑目录在归档后清理。复跑时可把 `--output` 指向本模型 `reports/<new-run>`；该目录不参与模型源码哈希，避免构建产物递归纳入证据。
