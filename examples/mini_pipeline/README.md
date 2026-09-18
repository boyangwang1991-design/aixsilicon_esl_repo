# mini_pipeline 示例（外存 → DMA → 向量计算 → DMA 回写）

验证公共组件复用、数据流、资源竞争、buffer 生命周期与性能解释的基础系统。
权威参数/调度/预期见 [`docs/esl_testcase.md`](../../docs/esl_testcase.md)（§3–§10）。

## 用途

回答"计算引擎配一套共享 DMA，单缓冲改双缓冲能隐藏多少搬运时间、剩余瓶颈在哪"；
扩展回答 DMA 带宽/lane 数/SRAM bank/容量限制等。

## 参数（默认）

| 参数 | 默认 | 说明 |
|---|---|---|
| N | 16384 | 元素数（int16 小端） |
| chunk | 256 元素 = 512B | 每 chunk 输入/输出 |
| num_slots | 1 | BMU slot 数（1/2/4/8） |
| dma_queue_depth | 4 | DMA 等待队列深度（≥1） |
| dma_bytes_per_tick | 32 | 聚合搬运速度（32→20 ticks/chunk） |
| compute_elements_per_tick | 8 | 计算速度（8→36 ticks/chunk） |

计算：`y = clip_{int16}(2x+3)`；输入 `x[i]=((17i+5)%65536)-32768`。

## 解析预期（仅默认参数 + 指定调度）

- T01 单 chunk：76 ticks（load 20 + compute 36 + store 20）
- T02 64 chunk 单 slot：4864 ticks，DMA/Compute 不重叠
- T03 64 chunk 双 slot：2592 ticks，DMA/Compute 重叠
- E0/E1：slot=1 → 4864，slot=2 → 2592

## 重跑

```bash
# 端到端 CLI
PYTHONPATH=<esl_repo> uv run --project . python <esl_repo>/tools/esl_cli.py run examples/mini_pipeline/system.yaml
PYTHONPATH=<esl_repo> uv run --project . python <esl_repo>/tools/esl_cli.py sweep examples/mini_pipeline/experiment.yaml
# T 系列测试
PYTHONPATH=<esl_repo> uv run --project . python examples/mini_pipeline/tests/test_t01_t03.py
```

## 限制

- 基础 profile（pipeline_analytic）：聚合时间模型，未建模 bank/descriptor/RTL；
  tick 为资源模型内结果，非芯片预测。
- 外存/SRAM 零时延数据服务；仅 full_data（traffic_only 未实现）。
