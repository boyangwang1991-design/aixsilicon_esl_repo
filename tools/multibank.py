"""Configuration, reproducible runs and offline analysis for the SystemC multibank profile."""
from __future__ import annotations

import hashlib
import html
import importlib.metadata
import itertools
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path

import jsonschema
import yaml
from common_explore import read_events

ROOT = Path(__file__).resolve().parents[1]
SYSTEM = ROOT / 'systems/multibank'
SCHEMA = ROOT / 'contracts/multibank.schema.json'


class UniqueLoader(yaml.SafeLoader):
    pass


def mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if not isinstance(key, str) or key in result:
            raise ValueError('configuration keys must be unique strings')
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, mapping)


def load(path):
    return yaml.load(Path(path).read_text(), Loader=UniqueLoader)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def dump(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')


def topology(config):
    """Concrete fixed-profile ports, widths and address partitions used by C++."""
    ports, edges, components = {}, [], []
    width = config['bytes'] * 8
    def endpoint(name, direction, protocol):
        ports[name] = {'direction': direction, 'protocol': protocol, 'width_bits': width}
    def connect(source, target):
        edges.append({'from': source, 'to': target})
    for port in range(config['ports']):
        source = f'port{port}'
        components.append({'id': source, 'kind': 'workload_source', 'pending_capacity': 1,
                           'address_base': port * (config['capacity'] // config['ports']),
                           'address_span': config['capacity'] // config['ports']})
        endpoint(source + '.request', 'out', 'memory_request_v1')
        endpoint(source + '.response', 'in', 'memory_response_v1')
        endpoint(f'router.in{port}', 'in', 'memory_request_v1')
        endpoint(f'return.out{port}', 'out', 'memory_response_v1')
        connect(source + '.request', f'router.in{port}')
        connect(f'return.out{port}', source + '.response')
    components += [{'id': 'router', 'kind': 'address_router', 'mapping': config['mapping']},
                   {'id': 'return', 'kind': 'ordered_return', 'capacity': config['outstanding'],
                    'cycles': config['response_cycles'], 'responses_per_port_per_cycle': 1}]
    for bank in range(config['banks']):
        name = f'bank{bank}'
        components.append({'id': name, 'kind': config['storage'] + '_bank',
                           'capacity_bytes': config['capacity'] // config['banks'],
                           'outstanding': config['bank_capacity'], 'latency_cycles': config['latency'],
                           'initiation_cycles': config['interval'], 'arbitration': config['arbitration']})
        endpoint(f'router.out{bank}', 'out', 'memory_request_v1')
        endpoint(name + '.request', 'in', 'memory_request_v1')
        endpoint(name + '.response', 'out', 'memory_response_v1')
        endpoint(f'return.in{bank}', 'in', 'memory_response_v1')
        connect(f'router.out{bank}', name + '.request')
        connect(name + '.response', f'return.in{bank}')
    return {'profile': 'aixsilicon:esl:multibank:0.1.0', 'components': components, 'ports': ports, 'connections': edges}


def resolve(document):
    schema = json.loads(SCHEMA.read_text())
    try:
        jsonschema.Draft202012Validator(schema).validate(document)
    except jsonschema.ValidationError as error:
        raise ValueError('/'.join(map(str, error.path)) + ': ' + error.message) from error
    config = {key: value['default'] for key, value in schema['properties'].items() if 'default' in value}
    config.update(document)
    checks = [
        (config['bytes'] <= config['stripe'] and config['stripe'] % config['bytes'] == 0, 'bytes/stripe alignment'),
        (config['capacity'] % (config['banks'] * config['stripe']) == 0, 'capacity/bank/stripe geometry'),
        (config['capacity'] % config['ports'] == 0 and (config['capacity'] // config['ports']) % config['bytes'] == 0, 'source address partitions'),
        (config['stride'] % config['bytes'] == 0, 'stride alignment'),
        (config['requests'] * config['ports'] <= 100000, 'workload size limit'),
        (config['warmup_cycles'] < config['max_cycles'] and config['phase_cycles'] <= config['max_cycles'], 'warmup/phase/watchdog'),
        (config['mapping'] != 'xor' or not config['banks'] & (config['banks'] - 1), 'XOR requires power-of-two banks'),
    ]
    for okay, reason in checks:
        if not okay:
            raise ValueError(reason)
    graph = topology(config)
    if 'connections' in config:
        expected = {(edge['from'], edge['to']) for edge in graph['connections']}
        actual, used = set(), set()
        for edge in config['connections']:
            source, target = edge['from'], edge['to']
            if source not in graph['ports'] or target not in graph['ports']:
                raise ValueError(f'unknown endpoint: {source} -> {target}')
            a, b = graph['ports'][source], graph['ports'][target]
            if a['direction'] != 'out' or b['direction'] != 'in' or a['protocol'] != b['protocol'] or a['width_bits'] != b['width_bits']:
                raise ValueError(f'incompatible connection: {source} -> {target}')
            if source in used or target in used:
                raise ValueError('endpoint connected more than once')
            used.update((source, target))
            actual.add((source, target))
        if actual != expected:
            raise ValueError('unconnected endpoint or unsupported rewiring in fixed multibank profile')
    config['connections'] = graph['connections']
    return config, graph


def write_resolved(config, directory):
    dump(directory / 'resolved.json', config)
    (directory / 'resolved.cfg').write_text(''.join(f'{key}={value}\n' for key, value in config.items() if key != 'connections'))


def source_hashes():
    paths = []
    for folder in ('systems/multibank', 'common', 'primitives', 'infrastructure', 'adapters',
                   'services', 'workloads', 'verification', 'cmake', 'contracts'):
        paths += [p for p in (ROOT / folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    paths += [Path(__file__), ROOT / 'tools/common_explore.py', ROOT / 'tools/esl_cli.py']
    return {str(path.relative_to(ROOT)): digest(path) for path in sorted(paths)}


def command(argv, log, timeout=180):
    with Path(log).open('w') as stream:
        result = subprocess.run([str(arg) for arg in argv], stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f'exit {result.returncode}; see {Path(log).name}')


def build(directory, cmake):
    cmake = shutil.which(cmake)
    if not cmake:
        raise ValueError('CMake not found')
    command([cmake, '--version'], directory / 'cmake-version.log')
    command([cmake, '-S', SYSTEM, '-B', directory / 'build', f'-DESL_MODELS_SOURCE_DIR={ROOT}'], directory / 'configure.log')
    command([cmake, '--build', directory / 'build', '-j4'], directory / 'build.log')
    return directory / 'build/multibank_run'


def analyze(path, config, summary):
    tick_seconds, dropped, rows = read_events(path)
    if not math.isclose(tick_seconds, 1e-12, rel_tol=0, abs_tol=1e-24):
        raise ValueError('multibank event clock must be 1 ps')
    begin, end = config['warmup_cycles'] * config['period_ps'], summary['finish_cycle'] * config['period_ps']
    stages = ('enqueue', 'accept', 'service', 'complete', 'response_ready', 'retire')
    transactions, waits = {}, {}
    for row in rows:
        tag, tick, phase = int(row['id']), int(row['tick']), row['phase']
        if row['source'] not in {f'port{p}' for p in range(config['ports'])} or tick > end:
            raise ValueError('event source/time outside run')
        transaction = transactions.setdefault(tag, {'id': tag, 'source': row['source'], 'stages': {}, 'waits': {}})
        if row['source'] != transaction['source']:
            raise ValueError('transaction source changed')
        if phase.startswith('wait_'):
            if phase not in ('wait_bank_capacity', 'wait_bank_interval', 'wait_response_credit', 'wait_arbitration'):
                raise ValueError('unknown waiting reason')
            if 'enqueue' not in transaction['stages'] or 'accept' in transaction['stages']:
                raise ValueError('waiting outside ingress phase')
            if tick >= begin:
                waits[phase] = waits.get(phase, 0) + 1
            transaction['waits'][phase] = transaction['waits'].get(phase, 0) + 1
        elif phase in ('read', 'write'):
            if 'operation' in transaction or 'service' not in transaction['stages'] or 'complete' in transaction['stages']:
                raise ValueError('invalid storage operation phase')
            if row['resource'] != transaction['bank'] or int(row['value']) != config['bytes']:
                raise ValueError('storage operation resource/bytes mismatch')
            transaction['operation'] = phase
        elif phase in stages:
            index = stages.index(phase)
            if phase in transaction['stages'] or (index and stages[index - 1] not in transaction['stages']):
                raise ValueError('duplicate or unordered transaction stage')
            transaction['stages'][phase] = tick
            if phase == 'accept':
                transaction.update(address=int(row['value']), bank=row['resource'])
            if phase in ('service', 'complete') and row['resource'] != transaction['bank']:
                raise ValueError('bank changed within service')
        else:
            raise ValueError(f'unknown event phase: {phase}')
    complete = not dropped and summary['status'] == 'PASS'
    if complete and (len(transactions) != summary['completed'] or any('retire' not in t['stages'] or 'operation' not in t for t in transactions.values())):
        raise ValueError('event/summary transaction conservation')
    samples = [t['stages']['retire'] - t['stages']['accept'] for t in transactions.values()
               if 'retire' in t['stages'] and t['stages']['accept'] >= begin]
    samples.sort()
    if complete and (len(samples) != summary['measured_transactions'] or
                     sum(samples) != summary['measured_latency_cycles'] * config['period_ps']):
        raise ValueError('event/summary measurement mismatch')
    def percentile(q):
        return samples[math.ceil(len(samples) * q) - 1] if complete and samples else None
    width = max(1, math.ceil(max(0, end - begin) / 16))
    bins = math.ceil(max(0, end - begin) / width)
    heat = {f'bank{bank}': [0.0] * bins for bank in range(config['banks'])}
    queue = {f'port{port}': 0 for port in range(config['ports'])}
    return_bytes = {port: 0 for port in queue}
    for t in transactions.values():
        s = t['stages']
        if 'accept' in s:
            queue[t['source']] += max(0, min(end, s['accept']) - max(begin, s['enqueue']))
        if 'complete' in s:
            for i in range(bins):
                left, right = begin + i * width, min(end, begin + (i + 1) * width)
                heat[t['bank']][i] += max(0, min(right, s['complete']) - max(left, s['service'])) / (right - left)
        if 'retire' in s and begin <= s['retire'] <= end:
            return_bytes[t['source']] += config['bytes']
    return {'complete': complete, 'trace_dropped': dropped, 'tick_seconds': tick_seconds,
            'window_begin_tick': begin, 'window_end_tick': end, 'window_ticks': max(0, end - begin),
            'p95_ticks': percentile(.95), 'p99_ticks': percentile(.99), 'sample_count': len(samples),
            'percentile': 'nearest-rank; accepted at/after warmup; drained tails included',
            'wait_request_cycles': waits, 'queue_occupied_ticks': queue,
            'returned_bytes': return_bytes, 'bank_average_active': heat,
            'window_bin_ticks': width, 'transactions': list(transactions.values())}


def topology_svg(graph):
    sources = [c for c in graph['components'] if c['kind'] == 'workload_source']
    banks = [c for c in graph['components'] if c['id'].startswith('bank')]
    height = max(len(sources), len(banks)) * 65 + 100
    positions = {c['id']: (70, 50 + index * 65) for index, c in enumerate(sources)}
    positions.update({c['id']: (590, 50 + index * 65) for index, c in enumerate(banks)})
    positions.update(router=(330, 50), **{'return': (330, height - 60)})
    parts = [f'<svg viewBox="0 0 760 {height}" width="1000" role="img" aria-label="Resolved component topology">',
             '<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="5" markerHeight="5" orient="auto-start-reverse"><path d="M 0 0 L 10 5 L 0 10 z" fill="#666"/></marker></defs>']
    for edge in graph['connections']:
        source, target = edge['from'].split('.')[0], edge['to'].split('.')[0]
        x1, y1 = positions[source]
        x2, y2 = positions[target]
        direction = 1 if x2 > x1 else -1
        x1 += 55 * direction
        x2 -= 55 * direction
        label = f'{edge["from"]} → {edge["to"]}; {graph["ports"][edge["from"]]["width_bits"]} bits'
        parts.append(f'<path d="M{x1},{y1} C{(x1+x2)/2},{y1} {(x1+x2)/2},{y2} {x2},{y2}" stroke="#888" fill="none" marker-end="url(#arrow)"><title>{html.escape(label)}</title></path>')
    for component in graph['components']:
        x, y = positions[component['id']]
        parts.append(f'<rect x="{x-55}" y="{y-18}" width="110" height="36" rx="5" fill="#e8f2fb" stroke="#247bd0"><title>{html.escape(json.dumps(component))}</title></rect>')
        parts.append(f'<text x="{x}" y="{y+5}" text-anchor="middle">{html.escape(component["id"])}</text>')
    parts.append('</svg>')
    return '\n'.join(parts)


def report(config, graph, summary, analysis):
    parts = ['<!doctype html><meta charset="utf-8"><title>ESL multibank run</title>',
             '<style>body{font:15px sans-serif;margin:2em}table{border-collapse:collapse}td,th{border:1px solid #bbb;padding:5px}svg{max-width:100%}</style>',
             '<h1>SystemC multibank</h1>', '<p>' + html.escape(json.dumps(summary)) + '</p>',
             '<h2>Resolved topology</h2>', topology_svg(graph),
             '<table><tr><th>Component</th><th>Actual parameters</th></tr>']
    for component in graph['components']:
        parts.append('<tr><td>' + html.escape(component['id']) + '</td><td>' + html.escape(json.dumps(component)) + '</td></tr>')
    parts.append('</table><details><summary>Validated connections</summary><pre>' + html.escape(json.dumps(graph['connections'], indent=2)) + '</pre></details>')
    if analysis is None:
        parts.append('<p>Observation is disabled or counters-only; no timeline inferred.</p>')
        return '\n'.join(parts)
    parts.append('<p>Trace complete: ' + str(analysis['complete']) + '. Partial traces are diagnostic only; latency percentiles require a complete run.</p>')
    parts.append('<p>Latency p95/p99 (ps): ' + str(analysis['p95_ticks']) + ' / ' + str(analysis['p99_ticks']) + '</p>')
    parts.append('<h2>Waiting causes (request-cycles)</h2><pre>' + html.escape(json.dumps(analysis['wait_request_cycles'], indent=2)) + '</pre>')
    parts.append('<h2>Ingress occupancy and return traffic</h2><table><tr><th>Source</th><th>Occupied ticks</th><th>Returned bytes</th></tr>')
    for port, ticks in analysis['queue_occupied_ticks'].items():
        parts.append(f'<tr><td>{html.escape(port)}</td><td>{ticks}</td><td>{analysis["returned_bytes"][port]}</td></tr>')
    parts.append('</table><h2>Bank heat: average active service transactions</h2><p>Warmup excluded; overlap may exceed one. This is not utilization.</p><table>')
    peak = max((x for row in analysis['bank_average_active'].values() for x in row), default=1) or 1
    for bank, row in analysis['bank_average_active'].items():
        parts.append('<tr><th>' + bank + '</th>' + ''.join(f'<td style="background:rgba(20,120,220,{x/peak:.3f})">{x:.2f}</td>' for x in row) + '</tr>')
    parts.append('</table><h2>Address mapping and phase timeline</h2><p>Orange: ingress; blue: bank service; green: response delay/order. No reassembly phase exists in this single-bank request profile.</p>')
    transactions = analysis['transactions'][:1000]
    parts.append(f'<p>Showing {len(transactions)} of {len(analysis["transactions"])} recorded transactions.</p>')
    scale = 850 / max(1, summary['finish_cycle'] * config['period_ps'])
    parts.append(f'<svg width="1150" height="{24*len(transactions)+20}" role="img">')
    for i, transaction in enumerate(transactions):
        s = transaction['stages']
        label = f'{transaction["id"]} {transaction.get("bank", "queued")} @{transaction.get("address", "?")}'
        parts.append(f'<text x="0" y="{i*24+15}">{html.escape(label)}</text>')
        for start, finish, color in [('enqueue', 'accept', '#e99a2e'), ('service', 'complete', '#247bd0'), ('complete', 'retire', '#28925a')]:
            if finish in s:
                parts.append(f'<rect x="{260+s[start]*scale}" y="{i*24}" width="{max(1,(s[finish]-s[start])*scale)}" height="18" fill="{color}"/>')
    parts.append('</svg>')
    return '\n'.join(parts)


def run(document, output, executable=None, workload=None, cmake='cmake', timeout=30):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest = {'schema_version': 1, 'profile': 'aixsilicon:esl:multibank:0.1.0', 'status': 'RUNNING',
                'backend': 'SystemC', 'source_sha256': source_hashes(), 'build_provenance': 'external-unverified' if executable else 'built-from-recorded-source'}
    manifest['host_dependencies'] = {'python': sys.version, 'PyYAML': yaml.__version__,
                                     'jsonschema': importlib.metadata.version('jsonschema')}
    try:
        dump(output / 'requested.json', document)
        config, graph = resolve(document)
        write_resolved(config, output)
        dump(output / 'topology.json', graph)
        shutil.copy2(SYSTEM / 'capabilities.json', output / 'capabilities.json')
        if workload:
            shutil.copyfile(Path(workload).resolve(), output / 'input.trace')
        binary = Path(executable).resolve() if executable else build(output, cmake)
        manifest.update(executable=str(binary), executable_sha256=digest(binary), seed=config['seed'],
                        random_algorithm='SplitMix64/FNV-1a named streams', timeout_seconds=timeout)
        argv = [str(binary), str(output / 'resolved.cfg'), str(output / 'input.trace') if workload else '-', str(output / 'simulation')]
        manifest['command'] = argv
        command(argv, output / 'simulation.log', timeout)
        summary = json.loads((output / 'simulation.summary.json').read_text())
        if summary['status'] != 'PASS' or summary['accepted'] != summary['completed']:
            raise ValueError('simulation did not pass/drain')
        analysis = analyze(output / 'simulation.events.csv', config, summary) if config['observation'] == 'trace' else None
        if analysis is not None:
            dump(output / 'analysis.json', analysis)
        if manifest['source_sha256'] != source_hashes() or manifest['executable_sha256'] != digest(binary):
            raise ValueError('source/executable changed during run')
        manifest.update(status='PASS', summary=summary, workload_sha256=digest(output / 'simulation.workload.trace'))
        (output / 'report.html').write_text(report(config, graph, summary, analysis))
    except (ValueError, TypeError, KeyError, OSError, RuntimeError, subprocess.TimeoutExpired, jsonschema.ValidationError) as error:
        manifest.update(status='FAIL', error=str(error))
        (output / 'report.html').write_text('<!doctype html><meta charset="utf-8"><h1>Run failed</h1><pre>' + html.escape(str(error)) + '</pre>')
    finally:
        manifest['artifacts_sha256'] = {str(p.relative_to(output)): digest(p) for p in sorted(output.iterdir()) if p.is_file() and p.name != 'run.json'}
        cache = output / 'build/CMakeCache.txt'
        if cache.is_file():
            manifest['artifacts_sha256']['build/CMakeCache.txt'] = digest(cache)
        dump(output / 'run.json', manifest)
    return manifest


def _sweep(document, output, cmake='cmake', timeout=30):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    if not isinstance(document, dict) or set(document) != {'base', 'matrix'} or not isinstance(document['matrix'], dict):
        raise ValueError('sweep requires base and matrix mappings')
    matrix = document['matrix']
    if not matrix or any(not isinstance(v, list) or not v for v in matrix.values()):
        raise ValueError('sweep axes must be nonempty lists')
    if math.prod(len(v) for v in matrix.values()) > 256:
        raise ValueError('sweep exceeds 256 points')
    # Build once. Each child records that exact binary; parent binds its build to source.
    hashes = source_hashes()
    binary = build(output, cmake)
    points = []
    for index, values in enumerate(itertools.product(*matrix.values())):
        config = dict(document['base'])
        config.update(zip(matrix, values, strict=True))
        child = run(config, output / f'point-{index:03d}', binary, cmake=cmake, timeout=timeout)
        points.append({'name': f'point-{index:03d}', 'status': child['status'], 'config': config, 'workload_sha256': child.get('workload_sha256'),
                       'summary': child.get('summary'), 'error': child.get('error')})
    baseline = points[0]
    for point in points:
        if baseline['status'] == point['status'] == 'PASS':
            a, b = baseline['summary'], point['summary']
            if baseline['workload_sha256'] == point['workload_sha256'] and a['completed'] == b['completed'] and a['memory_hash'] == b['memory_hash'] and a['period_ps'] == b['period_ps'] and b['finish_cycle']:
                point['speedup_vs_first_point'] = a['finish_cycle'] / b['finish_cycle']
    status = 'PASS' if all(p['status'] == 'PASS' for p in points) and hashes == source_hashes() else 'FAIL'
    result = {'schema_version': 1, 'status': status, 'backend': 'SystemC', 'source_sha256': hashes,
              'executable_sha256': digest(binary), 'points': points}
    dump(output / 'sweep.json', result)
    (output / 'report.html').write_text('<!doctype html><meta charset="utf-8"><h1>SystemC sweep</h1>' + ''.join(
        f'<p><a href="{p["name"]}/report.html">{p["name"]}</a> {p["status"]} '
        + html.escape(json.dumps({k: p[k] for k in ('config', 'error', 'speedup_vs_first_point') if k in p})) + '</p>' for p in points))
    return result


def sweep(document, output, cmake='cmake', timeout=30):
    output = Path(output).resolve()
    if output.exists():
        raise FileExistsError(f'{output} exists; refusing overwrite')
    try:
        return _sweep(document, output, cmake, timeout)
    except FileExistsError:
        raise
    except (ValueError, TypeError, KeyError, OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        output.mkdir(parents=True, exist_ok=True)
        result = {'schema_version': 1, 'status': 'FAIL', 'error': str(error), 'backend': 'SystemC'}
        dump(output / 'sweep.json', result)
        (output / 'report.html').write_text('<!doctype html><meta charset="utf-8"><h1>Sweep failed</h1><pre>' + html.escape(str(error)) + '</pre>')
        return result


def main(args):
    if args.timeout <= 0:
        raise ValueError('timeout must be positive')
    if args.action != 'run' and (args.workload or args.executable):
        raise ValueError('--workload/--executable are only supported for run')
    if args.action != 'resolve' and args.output is None:
        raise ValueError('--output is required for run/sweep')
    try:
        document = load(args.config)
    except (ValueError, OSError, yaml.YAMLError) as error:
        if args.action == 'resolve':
            raise
        output = Path(args.output).resolve()
        output.mkdir(parents=True, exist_ok=False)
        failure = {'schema_version': 1, 'status': 'FAIL', 'error': str(error), 'backend': 'SystemC', 'stage': 'load_configuration'}
        if args.config.is_file():
            shutil.copyfile(args.config, output / 'requested.yaml')
            failure['input_sha256'] = digest(output / 'requested.yaml')
        dump(output / ('sweep.json' if args.action == 'sweep' else 'run.json'), failure)
        (output / 'report.html').write_text('<!doctype html><meta charset="utf-8"><h1>Configuration failed</h1><pre>' + html.escape(str(error)) + '</pre>')
        print(json.dumps(failure))
        return 1
    if args.action == 'resolve':
        config, graph = resolve(document)
        print(json.dumps({'resolved': config, 'topology': graph}, indent=2))
        return 0
    if args.output is None:
        raise ValueError('--output is required for run/sweep')
    result = sweep(document, args.output, args.cmake, args.timeout) if args.action == 'sweep' else run(document, args.output, args.executable, args.workload, args.cmake, args.timeout)
    print(json.dumps({'status': result['status'], 'output': str(args.output)}, indent=2))
    return int(result['status'] != 'PASS')
