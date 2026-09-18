# 仓库验证入口

- tests/environment/systemc：SystemC 计数器与 TLM 环境冒烟，通过 cmake/ctest 构建运行。
- tests/test_cli_contracts.py：未知后端/资产/连接/参数、失败返回、模板原子性、独立 oracle 与时间判据、证据失效。
- tests/tools：环境安装/诊断等 Python 工具回归。
- tests/test_repository_layout.py：目录迁移、注册路径、模板输出与历史导入验证。
- models/ram/tests/systemc：RAM 行为/资源专项；其他模型组合测试由 examples/basic_system 与 examples/interrupt_system 提供。

`tools/validate_basic_models.py` 执行 RAM、源码消费者、安装消费者、移动 prefix 后消费者并保存新 run。Python 工具检查：从 workflow 根执行 `uv run python -m pytest repos/aixsilicon_esl_repo/tests`。历史 Python 测试另见 reference/legacy_python/README.md，不混入目标模型通过统计。
