"""Actual SystemC configuration/replay/failure/observation integration evidence."""
import argparse
import json
from pathlib import Path

import multibank as bank


def check(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cmake', default='cmake')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    result = {'status': 'RUNNING', 'source_sha256': bank.source_hashes(), 'cases': []}
    result['source_sha256']['tools/validate_multibank.py'] = bank.digest(__file__)
    try:
        binary = bank.build(output, args.cmake)
        def execute(name, config, workload=None, expected='PASS'):
            outcome = bank.run(config, output / name, binary, workload)
            check(outcome['status'] == expected, f'{name}: {outcome.get("error", outcome["status"])}')
            result['cases'].append({'name': name, 'expected_execution': expected, 'status': 'PASS'})
            return outcome
        baseline = execute('baseline', {'write_percent': 50})
        replay = execute('replay', {}, output / 'baseline/simulation.workload.trace')
        check(baseline['summary']['finish_cycle'] == replay['summary']['finish_cycle'] and
              baseline['summary']['memory_hash'] == replay['summary']['memory_hash'], 'replay behavior changed')
        for mode in ('off', 'counters'):
            other = execute(mode, {'observation': mode})
            check(other['summary']['finish_cycle'] == baseline['summary']['finish_cycle'] and
                  other['summary']['memory_hash'] == baseline['summary']['memory_hash'], 'observation altered behavior')
        for mode, config in [('sparse', {'storage': 'sparse'}), ('xor', {'mapping': 'xor'}),
                             ('priority', {'arbitration': 'priority'}), ('serialized', {'interval': 8}),
                             ('credits', {'outstanding': 1, 'bank_capacity': 1, 'response_cycles': 8})]:
            other = execute(mode, config)
            check(other['summary']['memory_hash'] == baseline['summary']['memory_hash'], 'policy/storage changed final data')
            if mode == 'serialized':
                check(other['summary']['finish_cycle'] > baseline['summary']['finish_cycle'], 'configured II did not affect service throughput')
            if mode == 'xor':
                observed = json.loads((output / mode / 'analysis.json').read_text())
                check(len({t['bank'] for t in observed['transactions']}) == 4, 'XOR configuration did not distribute physical bank accesses')
        dependency = output / 'dependencies.trace'
        dependency.write_text('# aix-esl-workload-v1 period_ps=1000\n'
                              '100 0 2 W 0 0102030405060708090a0b0c0d0e0f10 ff00 -\n'
                              '101 1 0 R 0 00000000000000000000000000000000 - 100\n')
        dependent = execute('dependencies', {'latency': 2, 'response_cycles': 3}, dependency)
        check(dependent['summary']['finish_cycle'] == 12, 'dependency must wait for retirement, not bank completion')
        memory = bytearray(4096)
        for offset in range(0, 16, 2):
            memory[offset] = offset + 1
        expected_hash = 14695981039346656037
        for value in memory:
            expected_hash = ((expected_hash ^ value) * 1099511628211) & ((1 << 64) - 1)
        check(dependent['summary']['memory_hash'] == str(expected_hash), 'independent masked-write oracle')
        execute('truncated', {'trace_limit': 1})
        partial = json.loads((output / 'truncated/analysis.json').read_text())
        check(not partial['complete'] and partial['p95_ticks'] is None, 'truncated trace claimed complete latency')
        execute('watchdog', {'max_cycles': 1}, expected='FAIL')
        check((output / 'watchdog/simulation.pending.json').is_file(), 'failed simulation lost pending diagnostic')
        execute('invalid_connection', {'connections': []}, expected='FAIL')
        check(not (output / 'invalid_connection/simulation.log').exists(), 'invalid topology executed')
        invalid = output / 'invalid.trace'
        invalid.write_text('# aix-esl-workload-v1 period_ps=1000\n0 0 0 W 0 00 - 1\n')
        execute('invalid_trace', {}, invalid, expected='FAIL')
        execute('warmup', {'warmup_cycles': 10})
        warmup = json.loads((output / 'warmup/analysis.json').read_text())
        check(warmup['window_begin_tick'] == 10000, 'measurement window ignored warmup')
        sweep = bank.sweep({'base': {}, 'matrix': {'mapping': ['stripe', 'xor'], 'storage': ['dense', 'sparse']}}, output / 'sweep', args.cmake)
        check(sweep['status'] == 'PASS' and len(sweep['points']) == 4, 'actual Cartesian sweep failed')
        failed_sweep = bank.sweep({'base': {}, 'matrix': {'banks': [4, 3]}}, output / 'failed-sweep', args.cmake)
        check(failed_sweep['status'] == 'FAIL' and len(failed_sweep['points']) == 2 and failed_sweep['points'][1]['status'] == 'FAIL', 'failed sweep point disappeared')
        result['cases'].append({'name': 'sweep_and_failed_points', 'status': 'PASS'})
        check(all(bank.digest(bank.ROOT / path) == sha for path, sha in result['source_sha256'].items()), 'source changed during verification')
        result['status'] = 'PASS'
    except (ValueError, RuntimeError, OSError) as error:
        result.update(status='FAIL', error=str(error))
    bank.dump(output / 'checks.json', result)
    print(json.dumps({'status': result['status'], 'evidence': str(output / 'checks.json')}))
    return int(result['status'] != 'PASS')


if __name__ == '__main__':
    raise SystemExit(main())
