"""Offline common SystemC fixture sweep and event analysis (no simulation in Python)."""
from __future__ import annotations

import argparse
import csv
import hashlib
import html
import itertools
import json
import math
import re
import subprocess
from pathlib import Path


def read_events(path: Path) -> tuple[float, int, list[dict]]:
    """Shared strict v1 transport reader; consumers validate their phase contracts."""
    with path.open() as stream:
        header = stream.readline().strip()
        match = re.fullmatch(r'# aix-esl-events-v1,tick_seconds=([0-9.eE+-]+),dropped=(\d+)', header)
        if not match:
            raise ValueError('event schema/header')
        tick_seconds, dropped = float(match[1]), int(match[2])
        if not math.isfinite(tick_seconds) or tick_seconds <= 0:
            raise ValueError('event tick unit')
        reader = csv.DictReader(stream)
        if reader.fieldnames != ['tick', 'id', 'parent', 'source', 'phase', 'resource', 'value']:
            raise ValueError('event columns')
        events = list(reader)
    previous = 0
    for event in events:
        if None in event or any(value is None for value in event.values()):
            raise ValueError('event row width')
        for key in ('tick', 'id', 'parent', 'value'):
            if not event[key].isascii() or not event[key].isdecimal():
                raise ValueError(f'event unsigned integer: {key}')
            if int(event[key]) > 2**64 - 1:
                raise ValueError(f'event uint64 overflow: {key}')
        tick = int(event['tick'])
        if tick < previous:
            raise ValueError('event time order')
        previous = tick
        for key in ('source', 'phase', 'resource'):
            if not re.fullmatch(r'[A-Za-z0-9_.:/-]+', event[key]):
                raise ValueError(f'event token: {key}')
    return tick_seconds, dropped, events


def analyze(path: Path) -> dict:
    """Composition v1 phase analysis; truncated traces cannot support latency claims."""
    tick_seconds, dropped, events = read_events(path)
    accepted, service, completed, retired = {}, {}, {}, {}
    samples, timeline, mapping = [], [], []
    previous = 0
    for event in events:
        tick, tag, value = (int(event[key]) for key in ('tick', 'id', 'value'))
        if tick < previous or min(tick, tag, value, int(event['parent'])) < 0:
            raise ValueError('event time/order/range')
        previous = tick
        phase = event['phase']
        table = {'accept': accepted, 'service': service, 'complete': completed, 'retire': retired}.get(phase)
        if table is None:
            raise ValueError(f'unsupported composition event: {phase}')
        if tag in table:
            raise ValueError(f'duplicate {phase}: {tag}')
        table[tag] = event
        if phase == 'accept':
            mapping.append({'id': tag, 'address': value, 'bank': event['resource']})
        elif phase == 'service':
            if tag not in accepted:
                raise ValueError('service without acceptance')
        elif phase == 'complete':
            if tag not in service:
                raise ValueError('completion without service')
            start = int(service[tag]['tick'])
            if event['resource'] != service[tag]['resource']:
                raise ValueError('service resource changed')
            timeline.append({'id': tag, 'source': event['source'], 'bank': event['resource'],
                             'start': start, 'end': tick, 'bytes': value})
        else:
            if tag not in completed:
                raise ValueError('retirement without completion')
            samples.append(tick - int(accepted[tag]['tick']))
    if not dropped and not (accepted.keys() == service.keys() == completed.keys() == retired.keys()):
        raise ValueError('incomplete transaction conservation')
    samples.sort()
    def percentile(percent: float):
        return samples[max(0, math.ceil(len(samples) * percent) - 1)] if samples and not dropped else None
    # Fixed 16-bin half-open windows; service overlap is reported as average
    # active transactions, not utilization (pipelined occupancy can exceed one).
    duration = max(1, previous)
    width = max(1, math.ceil(duration / 16))
    banks = sorted({span['bank'] for span in timeline})
    heat = {bank: [0.0] * math.ceil(duration / width) for bank in banks}
    for span in timeline:
        for index in range(len(heat[span['bank']])):
            left, right = index * width, min(duration, (index + 1) * width)
            overlap = max(0, min(right, span['end']) - max(left, span['start']))
            heat[span['bank']][index] += overlap / (right - left)
    return {'schema_version': 1, 'tick_seconds': tick_seconds, 'dropped': dropped,
            'complete_trace': not dropped, 'transactions': len(samples), 'duration_ticks': previous,
            'percentile_definition': 'nearest-rank', 'p95_ticks': percentile(.95),
            'p99_ticks': percentile(.99), 'mapping': mapping, 'timeline': timeline,
            'bank_average_active': heat, 'window_ticks': width}


def render(points: list[dict]) -> str:
    sections = ['<!doctype html><meta charset="utf-8"><title>ESL common experiment</title>',
                '<style>body{font:15px sans-serif;margin:2em}td,th{padding:5px;border:1px solid #ccc}'
                'table{border-collapse:collapse}svg{max-width:100%}</style>',
                '<h1>SystemC common composition</h1><p>32 writes, two sources, four banks; '
                'seed 17. Bank heat is average active transactions, not utilization. '
                'Latency starts at acceptance; injection waiting is excluded.</p>']
    for point in points:
        sections.append('<h2>' + html.escape(point['name']) + '</h2>')
        if point['status'] != 'PASS':
            sections.append('<p>FAILED: ' + html.escape(point.get('error', 'process failed')) + '</p>')
            continue
        data = point['analysis']
        sections.append(f'<p>Completion tick: {point["finish_tick"]}; '
                        f'p95: {data["p95_ticks"]}; p99: {data["p99_ticks"]}; '
                        f'tick seconds: {data["tick_seconds"]}</p>')
        sections.append('<h3>Address mapping</h3><table><tr><th>ID</th><th>Address</th><th>Bank</th></tr>')
        for entry in data['mapping']:
            sections.append(f'<tr><td>{entry["id"]}</td><td>{entry["address"]}</td>'
                            f'<td>{html.escape(entry["bank"])}</td></tr>')
        sections.append('</table><h3>Bank heat: average active transactions</h3><table>')
        maximum = max((v for values in data['bank_average_active'].values() for v in values), default=1) or 1
        for bank, values in data['bank_average_active'].items():
            cells = ''.join(f'<td style="background:rgba(30,120,210,{v/maximum:.3f})">{v:.2f}</td>' for v in values)
            sections.append('<tr><th>' + html.escape(bank) + '</th>' + cells + '</tr>')
        sections.append('</table><h3>Transaction service timeline</h3>')
        scale = 800 / max(1, data['duration_ticks'])
        sections.append(f'<svg width="1000" height="{24*len(data["timeline"])+20}" role="img">')
        for row, span in enumerate(data['timeline']):
            y = row * 24
            sections.append(f'<text x="0" y="{y+15}">{span["id"]} {html.escape(span["bank"])}</text>'
                            f'<rect x="{150+span["start"]*scale}" y="{y}" '
                            f'width="{max(1,(span["end"]-span["start"])*scale)}" height="18" fill="#2879ba"/>')
        sections.append('</svg>')
    return '\n'.join(sections)


def sweep(executable: Path, output: Path) -> int:
    executable = executable.resolve()
    output.mkdir(parents=True, exist_ok=False)
    points = []
    for mapping, latency, interval in itertools.product(('stripe', 'xor'), (2, 8), (1, 8)):
        name = f'{mapping}-lat{latency}-ii{interval}'
        point = {'name': name, 'mapping': mapping, 'latency_ns': latency, 'ii_ns': interval,
                 'seed': 17, 'status': 'FAIL'}
        try:
            times = []
            for observation in ('off', 'trace'):
                trace = output / f'{name}-{observation}.csv'
                command = [str(executable), mapping, observation, str(latency), str(interval), str(trace)]
                result = subprocess.run(command, capture_output=True, text=True, timeout=30)
                (output / f'{name}-{observation}.log').write_text(result.stdout + result.stderr)
                if result.returncode:
                    raise ValueError(f'{observation} exit {result.returncode}')
                match = re.search(r'completed=32 finish_tick=(\d+)', result.stdout)
                if not match:
                    raise ValueError('missing completion evidence')
                times.append(int(match[1]))
                if observation == 'trace':
                    point['analysis'] = analyze(trace)
            if times[0] != times[1]:
                raise ValueError('observation changed simulation time')
            point.update(status='PASS', finish_tick=times[0])
        except (ValueError, OSError, subprocess.TimeoutExpired) as error:
            point['error'] = str(error)
        points.append(point)
    baseline = next((p for p in points if p['name'] == 'stripe-lat8-ii8' and p['status'] == 'PASS'), None)
    for point in points:
        if baseline and point['status'] == 'PASS':
            point['speedup_vs_serial_baseline'] = baseline['finish_tick'] / point['finish_tick']
    manifest = {'schema_version': 1, 'backend': 'SystemC', 'executable': str(executable),
                'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
                'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'status': 'PASS' if all(p['status'] == 'PASS' for p in points) else 'FAIL', 'points': points}
    (output / 'results.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'report.html').write_text(render(points))
    return 0 if manifest['status'] == 'PASS' else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sweep_parser = sub.add_parser('sweep')
    sweep_parser.add_argument('--executable', type=Path, required=True)
    sweep_parser.add_argument('--output', type=Path, required=True)
    analyze_parser = sub.add_parser('analyze')
    analyze_parser.add_argument('trace', type=Path)
    args = parser.parse_args()
    if args.command == 'sweep':
        return sweep(args.executable, args.output)
    print(json.dumps(analyze(args.trace), indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
