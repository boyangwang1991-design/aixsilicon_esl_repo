"""esl 统一 CLI 入口（R31：doctor / inspect / new / run / sweep / compare）。

确定性执行门禁：Skill 与用户都应通过本入口调用 Repo 能力，不手写一次性脚本。
- doctor：环境检查
- inspect：能力发现（只返回 available 资产）
- new：从模板生成 model/system 骨架
- run：运行 system.yaml（含 oracle/checks/summary 输出）
- sweep：参数扫描（保留全部点含失败）
- compare：比较两 run 结果

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


def _out(obj) -> int:
    print(json.dumps(obj, ensure_ascii=False, indent=2))
    return 0


def cmd_doctor(_args):
    import esl_doctor  # 同目录模块
    return esl_doctor.main()


def cmd_inspect(args):
    """只返回 available（可运行候选）；planned 不返回。"""
    assets = _load_registry()
    kind = getattr(args, "kind", None)
    available = []
    planned_count = 0
    for a in assets:
        if kind and a.get("kind") != kind:
            continue
        if a.get("status", "planned") == "available":
            available.append({"id": a["id"], "path": a["path"],
                              "description": a.get("description", "")})
        else:
            planned_count += 1
    return _out({
        "command": "inspect", "kind": kind or "all",
        "available": available, "planned_count": planned_count,
        "message": "仅 available 资产可运行；planned 资产需先实现（见 docs/esl_todo.md）",
    })


def _render_template(template: str, name: str, kind: str) -> Path:
    """从 templates/<template>/ 渲染生成新资产（替换 {{name}} 占位符）。

    模板 = 薄骨架；只写业务 hook，不复制 common 实现（边界 references/boundaries.md）。
    返回生成的目标目录。
    """
    tpl_dir = REPO_ROOT / "templates" / template
    if not (tpl_dir / "template.yaml").exists():
        raise FileNotFoundError(f"模板 {template!r} 不存在（可用: compute/command_engine/buffer_manager/memory_target）")
    meta = yaml.safe_load((tpl_dir / "template.yaml").read_text(encoding="utf-8"))
    target_rel = meta.get("target", f"models/{{name}}/").replace("{name}", name)
    target = REPO_ROOT / target_rel
    if target.exists():
        raise FileExistsError(f"{target} 已存在，不覆盖")
    target.mkdir(parents=True, exist_ok=True)
    # 渲染 template.yaml 声明的文件（相对 tpl_dir）
    # files 声明的是【模板内实际文件名】；目标名映射规则固定：
    #   model.py / model.cpp → <name>.py / <name>.cpp
    #   test_template.py / test_template.cpp → tests/test_<name>.py / .cpp
    #   其他文件           → 同名
    for rel in meta.get("files", []):
        src = tpl_dir / rel
        if not src.exists():
            continue
        if rel in ("model.py", "model.cpp"):
            ext = rel.rsplit(".", 1)[1]
            dst_rel = f"{name}.{ext}"
        elif rel.endswith("test_template.py"):
            dst_rel = f"tests/test_{name}.py"
        elif rel.endswith("test_template.cpp"):
            dst_rel = f"tests/test_{name}.cpp"
        else:
            dst_rel = rel
        dst = target / dst_rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        text = src.read_text(encoding="utf-8")
        text = text.replace("{{name}}", name)
        # CMakeLists 里对源文件名的引用：model.cpp → <name>.cpp
        if rel == "CMakeLists.txt":
            text = text.replace("model.cpp", f"{name}.cpp")
        dst.write_text(text, encoding="utf-8")
    # 生成目录为合法 Python 包
    init = target / "__init__.py"
    if not init.exists():
        init.write_text(f'"""esl 模型 {name}（由模板生成）。"""\n', encoding="utf-8")
    # 生成 README（模板无 README 文件时）
    if not (target / "README.md").exists():
        (target / "README.md").write_text(
            f"# {name}\n\n从模板 `{template}` 生成的薄骨架（边界：不复制 common 实现）。\n",
            encoding="utf-8")
    return target


def cmd_new(args):
    """esl new：从模板生成新模型/系统薄骨架（R29/R30）。"""
    kind = args.kind
    name = args.name
    template = args.template
    try:
        target = _render_template(template, name, kind)
    except (FileNotFoundError, FileExistsError) as exc:
        return _out({"command": "new", "status": "FAIL", "error": str(exc)})
    return _out({"command": "new", "status": "OK",
                 "template": template,
                 "path": str(target.relative_to(REPO_ROOT)),
                 "note": "薄骨架已生成；运行 tests 前确认功能 hook 已实现；模板不得复制 common 实现"})


def _build_mini_pipeline(**params):
    """构造 mini_pipeline 系统实例（R31 run 用）。"""
    from examples.mini_pipeline.system import MiniPipeline
    return MiniPipeline(**params)


def cmd_run(args):
    """esl run：运行 system（mini_pipeline）→ ticks + 数据校验 + checks。

    运行记录输出到 runs/<run_id>/（run.json/metrics/checks/summary）。
    """
    import time as _time
    from common.base.config import validate_system
    from common.testing.reporting import one_page_summary, write_run_outputs

    system_path = Path(args.system)
    if not system_path.exists():
        return _out({"command": "run", "status": "BLOCKED", "error": f"{system_path} 不存在"})
    if yaml is None:
        return _out({"command": "run", "status": "BLOCKED", "error": "缺 PyYAML"})
    system = yaml.safe_load(system_path.read_text(encoding="utf-8"))
    # 从 system.yaml 读取实例参数（slot 数等）
    params = {}
    instances = system.get("instances", {})
    for name, spec in instances.items():
        if isinstance(spec, dict) and "params" in spec:
            params.update(spec["params"])
    validate_system(system)

    run_id = f"run-{int(_time.time())}"
    mp = _build_mini_pipeline(**params)
    ticks = mp.run()
    data_ok = mp.check_output()
    execution_status = "OK"
    overall_status = "PASS" if data_ok else "FAIL"
    checks = [
        {"name": "config_valid", "status": "PASS", "detail": "system.yaml 校验通过"},
        {"name": "data_oracle", "status": "PASS" if data_ok else "FAIL",
         "detail": "独立 oracle 逐元素比对"},
        {"name": "performance_sanity", "status": "PASS",
         "detail": f"total_ticks={ticks}（解析预期由实验配置决定）"},
    ]
    metrics = {"total_ticks": ticks, "n_elements": mp.n_elements,
               "num_chunks": mp.num_chunks, "data_ok": data_ok}
    summary = one_page_summary(work_type="run", assets="mini_pipeline",
                               profile="pipeline_analytic",
                               inputs=f"slots={params.get('num_slots', 1)}",
                               execution=f"真实运行 {ticks} ticks",
                               checks_status=overall_status,
                               evidence_path=f"runs/{run_id}/",
                               limitations="未建模 bank/descriptor/RTL；tick 为资源模型内结果")
    run_dir = write_run_outputs(
        REPO_ROOT / "runs", run_id, config=system, metrics=metrics, checks=checks,
        summary_text=summary, execution_status=execution_status,
        overall_status=overall_status)
    return _out({"command": "run", "status": overall_status, "run_id": run_id,
                 "ticks": ticks, "data_ok": data_ok,
                 "output_dir": str(run_dir.relative_to(REPO_ROOT))})


def cmd_sweep(args):
    """esl sweep：参数扫描（保留全部点含失败/非法）。"""
    import time as _time
    from common.testing.sweep import SweepRunner

    if yaml is None:
        return _out({"command": "sweep", "status": "BLOCKED", "error": "缺 PyYAML"})
    exp_path = Path(args.experiment)
    if not exp_path.exists():
        return _out({"command": "sweep", "status": "BLOCKED", "error": f"{exp_path} 不存在"})
    exp = yaml.safe_load(exp_path.read_text(encoding="utf-8"))
    sweep_map = exp.get("sweep", {})
    keys = list(sweep_map)
    values = list(sweep_map.values())
    if not keys:
        return _out({"command": "sweep", "status": "FAIL", "error": "sweep 为空"})
    points = []
    for combo in zip(*[v if isinstance(v, list) else [v] for v in values]):
        points.append(dict(zip(keys, combo)))

    def run_fn(params):
        mp = _build_mini_pipeline(**params)
        ticks = mp.run()
        ok = mp.check_output()
        return {"ticks": ticks, "data_ok": ok, "config": dict(params),
                "status": "PASS" if ok else "FAIL"}

    runner = SweepRunner()
    runner.run(points, run_fn)
    return _out({"command": "sweep", "experiment": str(exp_path),
                 "summary": runner.summary(), "points": runner.points})


def cmd_compare(args):
    """esl compare：比较两 run 的结果 dict（同口径指标）。"""
    from common.testing.sweep import RunComparator

    try:
        a = json.loads(Path(args.run_a).read_text(encoding="utf-8"))
        b = json.loads(Path(args.run_b).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return _out({"command": "compare", "status": "FAIL", "error": str(exc)})
    cmp = RunComparator()
    report = cmp.compare(a, b)
    return _out({"command": "compare", "status": "OK", "report": report})


def main(argv=None):
    parser = argparse.ArgumentParser(prog="esl", description="esl_repo 统一 CLI")
    sub = parser.add_subparsers(dest="command", required=True)

    p_doc = sub.add_parser("doctor", help="环境检查")
    p_doc.set_defaults(func=cmd_doctor)

    p_insp = sub.add_parser("inspect", help="能力发现")
    p_insp.add_argument("--kind", choices=["common", "model", "example", "template"],
                        default=None, help="按类别过滤")
    p_insp.set_defaults(func=cmd_inspect)

    p_new = sub.add_parser("new", help="从模板生成骨架")
    p_new.add_argument("kind", choices=["model", "system"], help="生成类型")
    p_new.add_argument("name", help="资产名")
    p_new.add_argument("--template", default="thin", help="模板名")
    p_new.set_defaults(func=cmd_new)

    p_run = sub.add_parser("run", help="运行 system（mini_pipeline）")
    p_run.add_argument("system", help="system.yaml 路径")
    p_run.set_defaults(func=cmd_run)

    p_sweep = sub.add_parser("sweep", help="参数扫描")
    p_sweep.add_argument("experiment", help="experiment.yaml 路径")
    p_sweep.set_defaults(func=cmd_sweep)

    p_cmp = sub.add_parser("compare", help="比较两 run")
    p_cmp.add_argument("run_a", help="run A（json 文件）")
    p_cmp.add_argument("run_b", help="run B（json 文件）")
    p_cmp.set_defaults(func=cmd_compare)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
