# esl_repo：面向架构探索的模型与公共组件库

本仓库提供轻量 ESL（Electronic System Level）架构探索的**工程资产**：可复用公共
组件、模型、模板、性能观察/测试工具与示例。**本仓库不承载方法**——工作方法由
Skill 仓库的 `esl-development-suite` 承载；两者通过版本化资产 ID、参数与工具结果
衔接。

规划权威文档：[`docs/esl_repo_plan.md`](docs/esl_repo_plan.md)（v1.3.0）。

## 定位

- **Repo 放资产，Skill 放方法**：本仓库的模型源码、公共组件、runtime、接口/schema、
  模板、generator、观察器、oracle、测试数据与示例系统；Skill 决定何时选择哪个模型、
  如何比较架构、如何解释结果。
- **不用 Skill 也应能运行示例**：通过本仓库的 CLI（`esl doctor / inspect / new /
  run / sweep / compare / bench`）即可完成基础探索。
- **默认轻量**：只维护三类检查——配置合法、结果正确、性能统计自洽。寄存器兼容、
  RTL 校准等按需增加。

## 目录结构

```text
esl_repo/
  README.md
  registry.yaml              # 资产注册表（ID/路径/类别/可用状态）
  contracts/                 # 少量 schema 和公共接口说明
  common/
    base/                    # 时间、ID、配置、错误、生命周期
    transport/               # 内存/命令/事件接口、TLM 适配
    resources/               # queue、server、arbiter、credit
    memory/                  # byte store、bank service
    registers/               # regbank、访问语义、IRQ helper
    observability/           # counters、trace、collector、export
    testing/                 # oracle、scenario、fault、microbench
  models/                    # DMA/BMU/Scheduler/Compute 等组合模型
  templates/                 # 模型、系统、实验的可运行起点
  examples/mini_pipeline/    # 基础行为/性能组合案例
  examples/multiport_sram_perf/ # 多AXI端口SRAM性能专项案例
  tools/                     # ESL 生成、构建、运行、分析等确定性工具资产
  docs/
  CMakeLists.txt
  pyproject.toml
```

## 快速开始（规划）

```bash
esl doctor                    # 环境检查（编译器/CMake/Python/SystemC）
esl inspect --kind performance  # 能力发现（只返回 available）
esl new model vector_affine --template compute
esl new system demo --template pipeline_system
esl run examples/mini_pipeline/system.yaml
esl sweep examples/mini_pipeline/experiment.yaml
esl compare runs/baseline runs/double_buffer
```

> 以上 CLI 与组件 API 均为**待开发目标**（P00–P14 实施中）；`registry.yaml` 中
> 的资产在实现前一律标 `planned`，不能当作 available 运行。

## 状态

- 实施阶段：P00–P14（进行中）。
- 基础验收（P18）标准见 [`docs/esl_todo.md`](docs/esl_todo.md)（TODO 与验收计划）。
