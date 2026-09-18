"""Asset discovery, validated generation and explicit legacy-reference execution."""
from __future__ import annotations

import argparse
import itertools
import json
import math
import struct
import sys
import tempfile
import uuid
from pathlib import Path

import yaml

# Also supports importlib-loaded test/host callers without requiring PYTHONPATH.
TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
import esl_contracts as contracts  # noqa: E402

REPO_ROOT = TOOLS.parent
LEGACY_ROOT = REPO_ROOT / 'reference/legacy_python'


def _out(obj):
    print(json.dumps(obj, ensure_ascii=False, indent=2))
    return 0 if obj.get('status') in {'PASS', 'OK'} else 1


def _use_legacy():
    if str(LEGACY_ROOT) not in sys.path:
        sys.path.insert(0, str(LEGACY_ROOT))


def cmd_inspect(args):
    assets = contracts.registry(REPO_ROOT)
    selected = [a for a in assets if not args.kind or a['kind'] == args.kind]
    requested = getattr(args, 'id', None)
    if requested:
        selected = [a for a in selected if requested in [a['id'], *a.get('aliases', [])]]
        if not selected:
            raise ValueError(f'unknown asset id: {requested}')
    available = []
    for asset in selected:
        if asset['status'] != 'available':
            continue
        item = dict(asset)
        item['evidence_status'] = contracts.evidence_status(REPO_ROOT, asset)
        if asset['kind'] == 'model':
            item['manifest'] = contracts.validate_model(REPO_ROOT / asset['path'] / 'model.yaml')
        elif asset['kind'] == 'common':
            item['manifest'] = contracts.validate_common(REPO_ROOT / asset['path'] / 'common.yaml')
        available.append(item)
    return _out({'command': 'inspect', 'status': 'PASS', 'available': available,
                 'resolved_id': selected[0]['id'] if requested else None,
                 'planned_count': sum(a['status'] == 'planned' for a in selected)})


def _render_template(template, name, kind, output=None):
    if not contracts.NAME.fullmatch(name) or not contracts.NAME.fullmatch(template):
        raise ValueError('name and template must be lowercase identifiers, not paths')
    entries = [a for a in contracts.registry(REPO_ROOT)
               if a['kind'] == 'template' and Path(a['path']).name == template]
    if len(entries) != 1 or entries[0]['status'] != 'available':
        raise ValueError(f'template {template!r} is missing or planned; generation refused')
    source = contracts.inside(REPO_ROOT, entries[0]['path'])
    meta = contracts.load(source / 'template.yaml')
    contracts.fields(meta, {'id', 'kind', 'files', 'support_files'}, {'id', 'kind', 'files', 'support_files'})
    if meta['id'] != entries[0]['id'] or meta['kind'] != kind:
        raise ValueError('template id/kind mismatch')
    target = (Path(output) if output else REPO_ROOT / 'models' / name).resolve()
    if target.exists():
        raise FileExistsError(f'{target} exists; refusing overwrite')
    replacements = {'{{name}}': name, '{{package}}': 'AixEsl' + ''.join(x.capitalize() for x in name.split('_'))}
    staged = {}
    for relative in meta['files']:
        src = contracts.inside(source, relative)
        text = src.read_text(encoding='utf-8')  # Missing files fail before creating a directory.
        dest = relative
        for token, value in replacements.items():
            dest, text = dest.replace(token, value), text.replace(token, value)
        contracts.inside(target, dest)
        if '{{' in text or '{{' in dest or dest in staged:
            raise ValueError(f'unresolved placeholder or duplicate destination: {relative}')
        staged[dest] = text
    for dest, relative in meta['support_files'].items():
        contracts.inside(target, dest)
        if dest in staged:
            raise ValueError('duplicate template support file')
        staged[dest] = contracts.inside(REPO_ROOT, relative).read_text(encoding='utf-8')
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.esl-new-', dir=target.parent) as temp:
        stage = Path(temp) / name
        stage.mkdir()
        for relative, text in staged.items():
            dst = stage / relative
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(text, encoding='utf-8')
        contracts.validate_model(stage / 'model.yaml')
        stage.rename(target)
    return target


def cmd_new(args):
    target = _render_template(args.template, args.name, args.kind, args.output)
    return _out({'command': 'new', 'status': 'OK', 'path': str(target),
                 'asset_status': 'unregistered', 'note': 'generated model needs its own validation before registration'})


def _build_mini_pipeline(**params):
    _use_legacy()
    from examples.mini_pipeline.system import MiniPipeline
    return MiniPipeline(**params)


def _execute_reference(system, backend):
    params = contracts.validate_reference(system, backend)
    mp = _build_mini_pipeline(**params)
    ticks = mp.run()
    # Independent arithmetic oracle: does not call ComputeShell/vector_affine.
    expected = b''.join(struct.pack('<h', max(-32768, min(32767, 2 * (((17 * i + 5) % 65536) - 32768) + 3)))
                        for i in range(params['n_elements']))
    data_ok = mp.ext_mem.read(0x10000, len(expected)) == expected
    chunks = params['n_elements'] // 256
    dma = 4 + (512 + params['dma_bytes_per_tick'] - 1) // params['dma_bytes_per_tick']
    compute = 4 + (256 + params['compute_elements_per_tick'] - 1) // params['compute_elements_per_tick']
    lower, upper = max(2 * chunks * dma, chunks * compute), chunks * (2 * dma + compute)
    timing_ok = type(ticks) is int and lower <= ticks <= upper
    return {'status': 'PASS' if data_ok and timing_ok else 'FAIL', 'backend': contracts.BACKEND,
            'scope': 'legacy_reference', 'target_validation': 'NOT_RUN', 'profile': 'pipeline_analytic',
            'time_unit': 'reference_tick', 'config': params, 'total_ticks': ticks, 'data_ok': data_ok,
            'checks': [{'name': 'config_valid', 'status': 'PASS'},
                       {'name': 'independent_data_oracle', 'status': 'PASS' if data_ok else 'FAIL'},
                       {'name': 'service_time_bounds', 'status': 'PASS' if timing_ok else 'FAIL',
                        'lower': lower, 'upper': upper, 'actual': ticks}]}


def cmd_run(args):
    system = contracts.load(args.system)
    result = _execute_reference(system, args.backend)
    run = REPO_ROOT / 'runs' / f'reference-{uuid.uuid4().hex}'
    run.mkdir(parents=True)
    result.update(command='run', output_dir=str(run))
    (run / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    (run / 'system.yaml').write_text(yaml.safe_dump(system, sort_keys=False))
    return _out(result)


def cmd_sweep(args):
    exp = contracts.load(args.experiment)
    contracts.fields(exp, {'schema_version', 'system', 'backend', 'baseline', 'sweep', 'metrics', 'mode'},
                     {'schema_version', 'system', 'sweep'})
    if exp['schema_version'] != 1:
        raise ValueError('unsupported experiment schema')
    system = contracts.load(Path(args.experiment).resolve().parent / exp['system'])
    backend = args.backend or exp.get('backend') or system.get('backend')
    if exp.get('backend', backend) != backend:
        raise ValueError('conflicting experiment backend')
    if not isinstance(exp.get('metrics', []), list) or not set(exp.get('metrics', [])) <= {'total_ticks', 'data_ok'}:
        raise ValueError('unsupported reference metric')
    contracts.validate_reference(system, backend)
    sweep, baseline = exp['sweep'], exp.get('baseline', {})
    contracts.fields(sweep, contracts.DEFAULTS)
    contracts.fields(baseline, contracts.DEFAULTS)
    if not sweep or any(not isinstance(v, list) or not v for v in sweep.values()):
        raise ValueError('sweep must contain nonempty parameter lists')
    keys, values = list(sweep), list(sweep.values())
    mode = exp.get('mode', 'cartesian')
    if mode == 'zip':
        if len({len(v) for v in values}) != 1:
            raise ValueError('zip sweep lengths must match; points cannot be truncated')
        combos = zip(*values, strict=True)
    elif mode == 'cartesian':
        combos = itertools.product(*values)
    else:
        raise ValueError('mode must be cartesian or zip')
    points = []
    for combo in combos:
        changes = {**baseline, **dict(zip(keys, combo, strict=True))}
        candidate = json.loads(json.dumps(system))
        for owner, names in contracts.OWNERS.items():
            candidate['instances'][owner].setdefault('params', {}).update({k: v for k, v in changes.items() if k in names})
        try:
            point = _execute_reference(candidate, backend)
        except Exception as exc:
            # A failed simulation must not discard other sweep points; interrupts still propagate.
            point = {'status': 'FAIL', 'error': str(exc)}
        points.append({'index': len(points), 'requested': changes, **point})
    return _out({'command': 'sweep', 'status': 'PASS' if all(p['status'] == 'PASS' for p in points) else 'FAIL',
                 'mode': mode, 'points': points, 'total': len(points)})


def cmd_compare(args):
    a, b = (json.loads(Path(p).read_text()) for p in [args.run_a, args.run_b])
    for key in ['backend', 'scope', 'profile', 'time_unit']:
        if not a.get(key) or a.get(key) != b.get(key):
            raise ValueError(f'incomparable runs: {key}')
    for result in [a, b]:
        if result.get('status') != 'PASS' or result.get('data_ok') is not True:
            raise ValueError('comparison requires successful validated runs')
        value = result.get('total_ticks')
        if type(value) not in {int, float} or not math.isfinite(value) or value <= 0:
            raise ValueError('missing/invalid total_ticks')
    if a['config']['n_elements'] != b['config']['n_elements']:
        raise ValueError('different workloads cannot be compared as equal work')
    return _out({'command': 'compare', 'status': 'PASS', 'time_unit': a['time_unit'],
                 'delta_ticks': b['total_ticks'] - a['total_ticks'], 'ratio': b['total_ticks'] / a['total_ticks']})


def main(argv=None):
    parser = argparse.ArgumentParser(prog='esl')
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('doctor')
    inspect = sub.add_parser('inspect')
    inspect.add_argument('--kind', choices=['common', 'model', 'example', 'template'])
    inspect.add_argument('--id', help='canonical or legacy alias; discovery only, never selects a run backend')
    validate = sub.add_parser('validate')
    validate.add_argument('--model', type=Path, help='model.yaml; otherwise validate available registry assets')
    validate.add_argument('--evidence', action='store_true', help='require current hashes for available assets')
    new = sub.add_parser('new')
    new.add_argument('kind', choices=['model', 'system'])
    new.add_argument('name')
    new.add_argument('--template', default='register_target')
    new.add_argument('--output', type=Path, help='new directory; never overwrite')
    for command, file in [('run', 'system'), ('sweep', 'experiment')]:
        p = sub.add_parser(command, help='explicit legacy reference backend only; SystemC uses CMake')
        p.add_argument(file)
        p.add_argument('--backend', help=f'currently supported: {contracts.BACKEND}')
    compare = sub.add_parser('compare')
    compare.add_argument('run_a')
    compare.add_argument('run_b')
    npu = sub.add_parser('npu-sram', help='SystemC SRAM model: build, run, validate and architecture exploration')
    npu.add_argument('action', choices=['run', 'validate', 'explore'])
    npu.add_argument('--output', type=Path, required=True)
    npu.add_argument('--config', type=Path, help='flat architecture YAML')
    npu.add_argument('--workload', type=Path, help='topological DAG trace')
    npu.add_argument('--quick', action='store_true', help='small search smoke; not full exploration')
    args = parser.parse_args(argv)
    try:
        if args.command == 'npu-sram':
            import npu_explore
            return npu_explore.main(args)
        if args.command == 'doctor':
            import esl_doctor
            return esl_doctor.main()
        if args.command == 'validate':
            if args.model:
                contracts.validate_model(args.model)
            else:
                assets = contracts.registry(REPO_ROOT)
                if args.evidence:
                    failures = {a['id']: contracts.evidence_status(REPO_ROOT, a)
                                for a in assets if a['status'] == 'available'
                                and contracts.evidence_status(REPO_ROOT, a) != 'CURRENT'}
                    if failures:
                        raise ValueError(f'evidence unavailable or stale: {failures}')
            return _out({'command': 'validate', 'status': 'PASS'})
        return globals()['cmd_' + args.command](args)
    except (OSError, ValueError, TypeError, KeyError, RuntimeError, yaml.YAMLError) as exc:
        return _out({'command': args.command, 'status': 'FAIL', 'error': str(exc)})


if __name__ == '__main__':
    sys.exit(main())
