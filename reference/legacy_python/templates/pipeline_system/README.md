# {{name}}（pipeline_system 模板生成）

组合既有模型的小系统。运行：

```bash
# 端到端（需 esl_repo 根为 PYTHONPATH）
PYTHONPATH=<esl_repo> uv run --project . python <esl_repo>/tools/esl_cli.py run examples/{{name}}/system.yaml
```

边界：本目录只含组合配置与 oracle；公共能力在 esl_repo common/models，不复制。
