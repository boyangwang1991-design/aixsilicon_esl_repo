# 历史 Python 原型

本目录保留 common、models、examples/mini_pipeline、templates 的原有内部结构，便于旧测试独立导入。顶层 ESL models/common/examples 已交给 SystemC 资产。这里的 PASS 仅证明历史原型自身，不表示目标模型/目标时间通过。

在 workflow 根复用 uv 环境：

```bash
uv run python repos/aixsilicon_esl_repo/tools/esl_cli.py run repos/aixsilicon_esl_repo/reference/legacy_python/examples/mini_pipeline/system.yaml
uv run python repos/aixsilicon_esl_repo/reference/legacy_python/common/base/tests/test_config_contracts.py
uv run python repos/aixsilicon_esl_repo/reference/legacy_python/common/observability/tests/test_observability.py
uv run python repos/aixsilicon_esl_repo/reference/legacy_python/examples/mini_pipeline/tests/test_t01_t03.py
```

CLI 仅在使用历史后端时加入本目录的 import 路径；独立脚本自行定位此目录，无须把它永久写入 PYTHONPATH。旧解析周期预期可能与现有实现不符，按实际结果报告，不继承历史 PASS。脚本内 REPO 指本历史子树；主 CLI 输出仍写资产仓 runs/。
