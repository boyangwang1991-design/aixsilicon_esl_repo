# esl_repo：面向架构探索的模型与公共组件库

本仓库提供轻量 ESL（Electronic System Level）架构探索的**工程资产**：可复用公共
组件、模型、模板、性能观察/测试工具与示例。**本仓库不承载方法**——工作方法由
Skill 仓库的 `esl-development-suite` 承载；两者通过版本化资产 ID、参数与工具结果
衔接。

继续开发先读 [资产计划与进度](docs/esl_todo.md)，场景与数值预期见 [测试案例](docs/esl_testcase.md)。

规划权威文档：[`docs/esl_repo_plan.md`](docs/esl_repo_plan.md)（v1.3.0）。

基础 IP 和基础系统规划见 [模型索引](docs/base_ip_models.md)，RAM、ROM、host_master、tlm_bus 与 B0 basic_system 已可运行；timer/irq_controller 及 [中断示例](examples/interrupt_system/README.md) 已完成 B1 子集，其余规划资产保持 planned。构建入口见 [basic_system](examples/basic_system/README.md)，验证见 [B0 记录](docs/basic_models_validation.md)。

## 定位

- **Repo 放资产，Skill 放方法**：本仓库的模型源码、公共组件、runtime、接口/schema、
  模板、generator、观察器、oracle、测试数据与示例系统；Skill 决定何时选择哪个模型、
  如何比较架构、如何解释结果。
- **不用 Skill 也应能运行示例**：通过本仓库的 CLI（`esl doctor / inspect / new /
  run / sweep / compare`）即可完成基础探索。
- **默认轻量**：只维护三类检查——配置合法、结果正确、性能统计自洽。寄存器兼容、
  RTL 校准等按需增加。

## 目录结构

实际目录和迁移路径唯一见 [仓库布局](docs/repository_layout.md)。

- [模型索引](docs/base_ip_models.md)
- [SystemC 系统示例](examples/README.md)
- [验证入口](tests/README.md)
- [历史 Python 原型](reference/README.md)

## CLI 探测与使用

```bash
esl doctor                    # 环境检查（编译器/CMake/Python/SystemC）
esl inspect --kind model        # 按资产类别发现；用途/profile 另读 model.yaml
esl validate                  # 资产与交付合同校验
esl validate --evidence       # 检查已关联运行证据的 hash
esl new model demo --template register_target --output /new/path
esl run reference/legacy_python/examples/mini_pipeline/system.yaml
esl sweep reference/legacy_python/examples/mini_pipeline/experiment.yaml
esl compare runs/reference-A/result.json runs/reference-B/result.json
```

> 示例命令须先由实际 `--help` 核对；源码入口为 `tools/esl_cli.py`。
> run/sweep 必须显式选择历史参考后端；非法对象或未支持的 SystemC 后端返回错误。
> SystemC 是目标模型主体，Python 仅作可选算法参考或工程辅助；当前资产完成范围见任务账本。

## 状态

当前实现与待办唯一维护在 [资产计划与进度](docs/esl_todo.md)。历史验收摘要不替代当前 SystemC 构建与标准集成证据。

当前 CLI 合同与迁移行为见 [工具合同](contracts/tool_contracts.md)，逐项集成范围见 [覆盖说明](docs/integration_coverage.md)。
