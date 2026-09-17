"""esl 统一 CLI 入口（R31 基础：doctor / inspect）。

确定性执行门禁：Skill 与用户都应通过本入口调用 Repo 能力，不手写一次性脚本。
当前实现：doctor（环境检查）、inspect（能力发现，只返回 available 资产）。
后续随 R04/R31 扩展 new / run / sweep / compare / bench。

运行：uv run python tools/esl_cli.py <command> [args]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

try:
    import yaml
except ImportError:  # pragma: no cover
    yaml = None

REPO_ROOT = Path(__file__).resolve().parent.parent
REGISTRY = REPO_ROOT / "registry.yaml"


def _load_registry():
    if yaml is None:
        raise RuntimeError("需要 PyYAML（workflow 根环境已含；uv run 即可）")
    data = yaml.safe_load(REGISTRY.read_text(encoding="utf-8"))
    return data.get("assets", [])


def cmd_doctor(_args):
    import esl_doctor  # 同目录模块
    return esl_doctor.main()


def cmd_inspect(args):
    """esl inspect：轻量能力发现。只返回 available（可运行候选）；
    planned 不返回，避免把未实现资产当可用。"""
    assets = _load_registry()
    kind = getattr(args, "kind", None)
    want_status = "available"
    rows = []
    for a in assets:
        if kind and a.get("kind") != kind:
            continue
        status = a.get("status", "planned")
        rows.append(a)
    available = [a for a in rows if a.get("status") == want_status]
    planned = [a for a in rows if a.get("status") == "planned"]
    out = {
        "command": "inspect",
        "kind": kind or "all",
        "available": [{"id": a["id"], "path": a["path"], "description": a.get("description", "")} for a in available],
        "planned_count": len(planned),
        "message": "仅 available 资产可运行；planned 资产需先实现（见 docs/esl_todo.md）",
    }
    print(json.dumps(out, ensure_ascii=False, indent=2))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(prog="esl", description="esl_repo 统一 CLI")
    sub = parser.add_subparsers(dest="command", required=True)

    p_doc = sub.add_parser("doctor", help="环境检查")
    p_doc.set_defaults(func=cmd_doctor)

    p_insp = sub.add_parser("inspect", help="能力发现")
    p_insp.add_argument("--kind", choices=["common", "model", "example", "template"],
                        default=None, help="按类别过滤")
    p_insp.set_defaults(func=cmd_inspect)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
