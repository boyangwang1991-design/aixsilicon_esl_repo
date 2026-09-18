"""{{name}} 报告生成（report 模板生成）。

边界：复用 one_page_summary/write_run_outputs；本文件只组装本次结论，
不复制 reporting 实现。
"""
from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO))

from common.testing.reporting import one_page_summary, write_run_outputs  # noqa: E402


def main():
    summary = one_page_summary(
        work_type="compare", assets="mini_pipeline", profile="pipeline_analytic",
        inputs="slot=1 vs slot=2", execution="真实运行 E0/E1",
        checks_status="PASS", evidence_path="runs/", limitations="未建模 bank/descriptor/RTL")
    write_run_outputs(REPO / "runs", "report-demo", config={"demo": True},
                      metrics={"speedup": 1.88}, checks=[{"name": "ok", "status": "PASS"}],
                      summary_text=summary)
    print("{{name}} 报告已生成 → runs/report-demo/summary.md")


if __name__ == "__main__":
    main()
