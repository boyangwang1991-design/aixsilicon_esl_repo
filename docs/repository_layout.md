# 仓库目录与迁移索引

本页只记录资产位置与实际入口；开发方法仍在 esl-development-suite。注册状态由 registry.yaml 管理，进度仅在 esl_todo.md 跟踪。

```text
models/<name>/               SystemC 可复用模型或明确标记的 planned 模型
  systemc/include/<name>/    公开 Config/Model
  systemc/src/               实现
  docs/                     design / integration / verification
  tests/systemc/            模型专项；组合检查可复用 examples 中的消费者
  examples/integration/     单模型外部消费者（同时承担 standalone）
common/systemc/include/      目标模型共用的内部 C++ 组件
examples/<system>/          实际 SystemC 系统装配、工作负载、scoreboard、CMake
  systemc/                  仿真顶层；不复制模型实现
  README.md                 配置、连接、运行与能力边界
tests/environment/systemc/ SystemC 工具链冒烟，不是系统示例
tests/tools/               Repo Python 工具的回归
tests/                     仓库结构/迁移集成检查
templates/                 SystemC 模板；未验证的草案不升级 available
reference/legacy_python/   历史 Python 原型及其完整依赖、模板和测试
contracts/                 仓库环境合同及接口定义
cmake/                     可复用模型打包与依赖发现
tools/                     确定性执行工具
docs/                      工程说明、唯一计划/账本/验证索引
runs/                      被忽略的运行日志、构建与 hash 证据
```

模型不建空 reference/python 目录。后续真正的算法 oracle 放所属模型的 reference/python；legacy_python 含旧调度和聚合时间实现，不是经过独立验证的算法 oracle，也不能作为 SystemC 时序证据。

## 本次路径迁移

| 原位置 | 唯一新位置 |
|---|---|
| examples/min_systemc | tests/environment/systemc |
| examples/mini_pipeline | reference/legacy_python/examples/mini_pipeline |
| common/{base,transport,resources,memory,observability,testing} | reference/legacy_python/common/ 下同名目录 |
| models/{dma,compute,bmu,scheduler}.py 与 __init__.py | reference/legacy_python/models/ |
| Python templates（compute/command_engine/buffer_manager/memory_target/pipeline_system/parameter_sweep/report） | reference/legacy_python/templates/ |
| tools/tests | tests/tools |

不保留复制实现或路径别名。旧 run 日志和 hash 是历史证据，不改写其原始路径；需要新证据时通过 tools/validate_basic_models.py 重跑。

## 接入兼容性

四个已有 SystemC 模型的公开头文件、CMake targets/packages 及 examples/basic_system 路径保持不变。环境冒烟使用新 tests/environment/systemc 路径。CLI inspect 仍从 registry 发现 available 模型；run/sweep/compare 仅按需加载 legacy_python，不能据此执行任意 SystemC 系统。

历史 Python 模板与 compute_cpp 保留为迁移参考但不可生成。CLI 仅允许已登记 available 的模板；标准库模板提供独立输出目录、公开 API 与完整消费者。

定时器/中断控制器已完成源码、模型文档与独立验证，纳入默认 SystemC 构建；UART/GPIO 等其余模型仍 planned。

## 迁移验收

SystemC B0 的 32 次 CTest 全部通过（含 source/install/relocated 三种消费者），证据 `runs/layout-refactor-20260918/checks.json`。新环境路径的 2 项 CTest 通过；7 项 Python 工具/迁移检查通过；旧 Python 配置/合同及 T01–T03 数据/周期检查通过。参考检查与目标检查分别统计。

本次没有变更四个可用模型的公共 ABI，当时未将 timer/irq_controller 草案升级可用；后续实现状态以 registry 和任务账本为准。全仓 make check / pre-commit 的状态单独记录，不用局部回归代替全仓门禁。
