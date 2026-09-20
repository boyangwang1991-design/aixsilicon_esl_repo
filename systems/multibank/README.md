# 可配置 SystemC 多 Bank 公共组件系统

`aixsilicon:esl:multibank:0.1.0` 是固定拓扑、配置驱动的公共能力试用系统。
组件与时序均在 C++/SystemC 中执行；Python 负责配置、构建、批次编排和离线分析。
支持范围见 capabilities.json；配置字段、默认值和标量范围以
[配置 schema](../../contracts/multibank.schema.json) 为唯一事实源。

## 运行与集成

在 ESL Repo 根目录执行（工作区中沿用根 uv 环境；CMake/SystemC 已安装）：

```bash
uv run python tools/esl_cli.py multibank resolve --config systems/multibank/configs/baseline.yaml
uv run python tools/esl_cli.py multibank run --config systems/multibank/configs/baseline.yaml --output runs/multibank-baseline
uv run python tools/esl_cli.py multibank sweep --config systems/multibank/configs/sweep.yaml --output runs/multibank-sweep
uv run python tools/esl_cli.py multibank run --config systems/multibank/configs/baseline.yaml --workload runs/multibank-baseline/simulation.workload.trace --output runs/multibank-replay
```

输出目录必须不存在，失败记录也不覆盖。`--cmake` 指定 CMake；`--timeout` 是实际仿真的墙钟秒数。
`--executable` 可使用外部二进制，但运行记录明确标为 external-unverified，不能推断其对应当前源码。
默认入口从本次记录的源码构建可执行文件。`run.json` 保存最终状态、命令、源码/二进制/制品哈希、seed、随机算法与统计；SystemC 版本由运行程序上报。

作为独立 CMake 消费者：

```bash
cmake -S systems/multibank -B build/multibank -DESL_MODELS_SOURCE_DIR="$PWD"
cmake --build build/multibank
ctest --test-dir build/multibank --output-on-failure
```

安装包消费时改为 `-DCMAKE_PREFIX_PATH=<AixEslCommon-prefix>`，不传源码路径。
只链接 `aix::esl::common`；没有第二套内核。默认解析配置 fixture `configs/default.cfg`
由 schema 默认值生成，并由工具测试检查一致；业务配置使用 YAML，未知/重复字段均拒绝。

## 配置与装配边界

可替换 ports/banks、stripe/xor/contiguous 映射、rr/priority 仲裁、dense/sparse 后端、
latency/interval、有限全局及 Bank credit、响应延迟、读写混合、流量模式、seed、端口相位和预热窗口。
所有请求固定宽度且对齐，必须落在一个 Bank；不声称 AXI pin-level、跨 Bank 重组、ECC 或检查点能力。

实际拓扑为 source request → router → Bank → ordered return → source response。
每个 source 有一个待提交位置，每个 Bank 每拍至多接受一次，每个 source 每拍至多退休一次。
Bank 服务 latency 和启动间隔 II 独立；服务完成至响应消费期间仍占用 Bank/global credit。
同 source 按准入顺序返回。Bank 存储容量为总容量均分，mapper 有逆映射检查。

`connections` 可显式列出 from/to；未提供时生成全部必要连接。端口名、方向、协议、位宽、重复绑定、
漏接与不支持的重连均检查。这里只允许声明中的固定拓扑，不能配置任意图后静默用另一套图执行。
源合成流量地址区间由容量均分且不重叠；回放允许不同源有意访问同一地址，由完成可见性决定顺序。

## 工作负载与观测

[工作负载合同](../../contracts/multibank.md) 定义可回放的 ID/source/earliest cycle/读写/data/mask/dependencies。
回放依赖在前驱响应退休后释放，拒绝的请求原样保留，不通过候选性能提前计算完成时刻。
`simulation.workload.trace` 是完整输入计划；`simulation.events.csv` 是可能截断的观测，不可互相替代。

仿真统计同时保留总量和预热后准入事务的延迟。测量从 warmup_cycles 开始，至排空完成，包含尾部响应。
无测量事务时不伪造 percentile。观察 off/counters/trace 不改变最终数据或目标完成时间。

离线 report.html 展示真实配置拓扑、映射、enqueue/service/response 时间线、Bank 平均活动事务、
ingress 占用积分、返回字节和等待原因（仲裁/Bank II/Bank 容量/全局响应 credit）。
等待计数单位为 request-cycles，多个请求可同时等待，不能解释为独占周期。
Bank 热图是并行服务占用，不是利用率；截断报告明确标记，p95/p99 不给完整结论。
无进展到 max_cycles 会失败并留下 pending.json、原始日志和事件，不以 sc_stop 冒充成功。

## 验证

`tools/validate_multibank.py` 构建并执行真实程序，验证记录/回放一致、观测开关、后端/策略替换、
依赖等待退休的 12-cycle 解析案例、独立 masked-write 数据 oracle、容量压力、预热、截断、
非法 trace/连接、watchdog 失败留存、实际笛卡尔扫描和失败点保留。
`tools/validate_basic_models.py` 额外验证源码/安装/搬迁三种消费路径。
