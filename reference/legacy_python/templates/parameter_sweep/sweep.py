"""{{name}} 参数扫描驱动（parameter_sweep 模板生成）。

边界：复用 SweepRunner；本文件只定义 run_fn（构造系统、运行、返回结果），
不复制 sweep/compare 实现。
"""
from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO))

from common.testing.sweep import SweepRunner  # noqa: E402
from examples.mini_pipeline.system import MiniPipeline  # noqa: E402


def run_fn(params):
    mp = MiniPipeline(**params)
    ticks = mp.run()
    ok = mp.check_output()
    return {"ticks": ticks, "data_ok": ok, "config": dict(params),
            "status": "PASS" if ok else "FAIL"}


def main():
    import yaml
    exp = yaml.safe_load((Path(__file__).parent / "experiment.yaml").read_text())
    sweep_map = exp["sweep"]
    keys = list(sweep_map)
    vals = list(sweep_map.values())
    points = [dict(zip(keys, combo)) for combo in zip(*vals)]
    runner = SweepRunner()
    runner.run(points, run_fn)
    print(runner.summary())
    for p in runner.points:
        print(f"  {p['params']} -> {p['status']} ticks={p.get('ticks')}")


if __name__ == "__main__":
    main()
