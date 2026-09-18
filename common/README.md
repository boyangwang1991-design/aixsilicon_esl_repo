# 公共组件（common/）

可编译/可复用的公共实现。**只此一份**；模型/示例不得复制（边界：见 skill
references/boundaries.md）。

## 目录

| 子目录 | 组件 | 用途 |
|---|---|---|
| `base/` | TickClock、IdAllocator、Epoch、错误类型、EventChannel、TaskGraph、config 校验/Factory | 时间/ID/生命周期/配置基础 |
| `transport/` | MemoryRequest、Command、Completion | 请求/命令/完成合同 |
| `resources/` | BoundedQueue、ServiceResource、Arbiter | 有限队列/非流水服务/仲裁 |
| `memory/` | ByteStore、BufferPool、MemoryPort | 真实字节存储/固定 slot/异步端口 |
| `observability/` | Observer、MetricRegistry、Aggregator、TraceRecorder、instrument | 埋点/聚合/trace/导出/自动埋点 |
| `testing/` | Oracle、ScenarioRunner、FaultInjector、SanityChecker、SweepRunner、RunComparator、reporting | oracle/场景/故障/自洽/扫描/比较/报告 |

## 运行/测试

```bash
# 组件冒烟（R06–R12）
PYTHONPATH=<esl_repo> uv run --project . python \
  <esl_repo>/common/base/tests/test_config_contracts.py
# 观察器（A06–A10）
PYTHONPATH=<esl_repo> uv run --project . python \
  <esl_repo>/common/observability/tests/test_observability.py
```

## 修改纪律

- 修一次公共组件让所有模型受益；改 Queue/Memory/Observe 需跑对应公共测试与
  小系统回归。
- 公共组件自带标准埋点（busy/queue 积分/字节计数），模型只补业务事实。
