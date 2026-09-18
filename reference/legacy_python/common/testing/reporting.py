"""R28：自动结果与简报（run.json / metrics.json / checks.json / summary.md）。

合同（规划 §5.5）：
- runs/<run_id>/ 输出：run.json（最终配置/源码/工具 revision/seed/状态）、
  metrics.json（单位/窗口/结果/assumptions）、checks.json（实际执行的检查）、
  summary.md（一页结论）、trace.jsonl（开启 trace 才输出）。
- 状态区分 execution_status / checks[].status / overall_status / 外层 test_status。
- 只引用既有输出，不把配置/指标抄入多份文档。
"""
from __future__ import annotations

import json
from pathlib import Path


def write_run_outputs(run_dir: Path, run_id: str, *, config=None, source_rev=None,
                      tool_versions=None, seed=None, metrics=None, checks=None,
                      summary_text: str = "", trace_lines=None,
                      execution_status="OK", overall_status="PASS") -> Path:
    """写出 runs/<run_id>/ 下的全部结果文件，返回 run_dir。"""
    run_dir = Path(run_dir) / run_id
    run_dir.mkdir(parents=True, exist_ok=True)

    run_json = {
        "run_id": run_id,
        "config": config or {},
        "source_revision": source_rev,
        "tool_versions": tool_versions or {},
        "seed": seed,
        "execution_status": execution_status,
        "overall_status": overall_status,
    }
    (run_dir / "run.json").write_text(json.dumps(run_json, ensure_ascii=False, indent=2),
                                      encoding="utf-8")

    if metrics is not None:
        (run_dir / "metrics.json").write_text(
            json.dumps(metrics, ensure_ascii=False, indent=2), encoding="utf-8")

    if checks is not None:
        (run_dir / "checks.json").write_text(
            json.dumps(checks, ensure_ascii=False, indent=2), encoding="utf-8")

    if summary_text:
        (run_dir / "summary.md").write_text(summary_text, encoding="utf-8")

    if trace_lines is not None:
        with open(run_dir / "trace.jsonl", "w", encoding="utf-8") as f:
            for line in trace_lines:
                f.write(line + "\n")

    return run_dir


def one_page_summary(*, work_type=None, assets=None, profile=None, inputs=None,
                     execution=None, checks_status=None, evidence_path=None,
                     limitations=None) -> str:
    """一页摘要模板（规划 §15.2 / 报告组织）。"""
    lines = [
        "# 实验摘要",
        f"- 工作类型: {work_type or '-'}",
        f"- 选中资产/profile: {assets or '-'} / {profile or '-'}",
        f"- 输入与实际配置: {inputs or '-'}",
        f"- 执行方式: {execution or '未运行'}",
        f"- 检查结果: {checks_status or '-'}",
        f"- 证据路径: {evidence_path or '-'}",
        f"- 限制: {limitations or '-'}",
    ]
    return "\n".join(lines) + "\n"
