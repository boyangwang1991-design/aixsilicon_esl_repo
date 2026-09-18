# ESL Repo / Skill 验收摘要（P18）

依据：esl-todo-list §9 验收结果记录要求（对象/范围/状态/证据/结论/重现）。

## 对象

| 项 | 值 |
|---|---|
| Skill repo revision | `b2bd372`（esl-development-suite，1 主入口 + 13 subskill K01–K13） |
| esl_repo revision | 见下方提交列表（骨架 → 公共组件 → 模型 → 观察/测试 → 配置 → CLI → 环境） |
| 环境 | Rocky 8.10；Python 3.12.13（uv）；g++ 8.5；CMake 4.4.3（uv venv）；SystemC 2.3.4（~/.local/systemc） |

esl_repo 提交链：`9e35999`（骨架）→ `5361db7`（R06–R12）→ `738143d`（R14–R18 模型）→
（R19–R28 观察/测试）→（R03–R05 配置/合同）→（R31 CLI + mini_pipeline 配置）→
（P16 行为评估）→（P17 W-A 验收）→（环境 K13：env_setup/min_systemc/doctor）。

## 范围

- **基础包（P00–P09）**：✅ 可声明基础可用。
- **SRAM 性能专项（P10–P11）**：⏳ 未实现（方法通过，执行未覆盖，独立验收）。
- **Skill Suite（P15–P17）**：✅ 13 入口落地，S-A 与 W-A 验收有证据。
- **专业扩展（X03/X04/X07 等）**：⏳ 未实现（明确列为未覆盖，不冒充）。

## 状态与证据

### 基础包

| 项 | 状态 | 证据 |
|---|---|---|
| R01–R12 环境/公共组件 | PASS | 组件冒烟测试（common/components） |
| R14–R18 模型 + mini_pipeline | PASS | T01=76 / T02=4864 / T03=2592 精确命中；数据 oracle 全对 |
| R19–R28 观察/测试 | PASS | A06–A10 通过（积分 1.1、utilization 0.3、overlap 4、单位换算、截断） |
| R03–R05 配置/合同 | PASS | test_config_contracts 全通过（VLNV/Factory/MemoryPort） |
| R31 CLI | PASS | `esl run` E0=4864/E1=2592 PASS；`esl sweep` 保留全部点 |
| 核心环境 | PASS | `esl doctor` 全 OK；min_systemc 用 CMake+SystemC 构建运行 count=11 |

### Skill 行为与路由

| 项 | 状态 | 证据 |
|---|---|---|
| S01–S18 + K13（13 入口） | PASS | validate_suite exit=0；宿主可发现 13 入口 |
| S-A01–S-A12 行为 | PASS 13/13 | p16_skill_evals.py |
| W-A01–W-A14 路由/方法 | PASS 14/14 | p17_wa_evals.py（9 真实执行 + 5 方法通过/执行未覆盖标注） |
| 资产边界 | PASS | validate_suite check_asset_boundary；Skill 目录仅 1 个 .py |

### 未覆盖项（明确列出，不冒充）

- SRAM 性能专项（X01/X07/X09–X11、M0–M8、MP01–MP07）：方法通过，执行未覆盖。
- 软件前端寄存器/descriptor（X03/X04）：未实现。
- 微架构细化 bank/Router（X02）：未实现。
- 校准（X12/X08）：无参考数据，校准任务 BLOCKED（符合判据）。
- RTL/ISS/DDR/NoC 详细后端、CDC/一致性：不在基础范围。

## 结论

- **基础可用**：公共组件、mini_pipeline（单/双缓冲）、观察/测试、配置/合同、CLI、
  环境（SystemC/CMake）均真实运行通过；Skill 方法层 13 入口落地且行为/路由验收有证据。
- **扩展未覆盖**：SRAM 专项、软件前端、微架构、校准按需实现后独立验收。
- 模型自洽 ≠ 硅片准确性；报告区分资源模型结果与真实芯片预测。

## 重现

```bash
# 1. 环境（workflow 根）
uv sync --extra dev
export SYSTEMC_HOME=$HOME/.local/systemc
export LD_LIBRARY_PATH=$SYSTEMC_HOME/lib-linux64:$LD_LIBRARY_PATH

# 2. 基础验收测试
PYTHONPATH=repos/aixsilicon_esl_repo uv run --project . python \
  repos/aixsilicon_esl_repo/examples/mini_pipeline/tests/test_t01_t03.py
PYTHONPATH=repos/aixsilicon_esl_repo uv run --project . python \
  repos/aixsilicon_esl_repo/common/observability/tests/test_observability.py
PYTHONPATH=repos/aixsilicon_esl_repo uv run --project . python \
  repos/aixsilicon_esl_repo/examples/mini_pipeline/tests/p16_skill_evals.py
PYTHONPATH=repos/aixsilicon_esl_repo uv run --project . python \
  repos/aixsilicon_esl_repo/examples/mini_pipeline/tests/p17_wa_evals.py

# 3. CLI 端到端
PYTHONPATH=repos/aixsilicon_esl_repo uv run --project . python \
  repos/aixsilicon_esl_repo/tools/esl_cli.py run repos/aixsilicon_esl_repo/examples/mini_pipeline/system.yaml
PYTHONPATH=repos/aixsilicon_esl_repo uv run --project . python \
  repos/aixsilicon_esl_repo/tools/esl_cli.py sweep repos/aixsilicon_esl_repo/examples/mini_pipeline/experiment.yaml

# 4. Skill 结构校验
uv run python repos/aixsilicon_skill_repo/skills/esl-development-suite/scripts/validate_suite.py
```
