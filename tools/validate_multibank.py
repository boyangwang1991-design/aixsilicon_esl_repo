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
        def snapshot(name):
            pending = json.loads((output / name / 'simulation.pending.json').read_text())
            check(pending['schema_version'] == 1, 'unknown pending schema')
            check(pending['outstanding'] == len(pending['flights']) ==
                  sum(item['outstanding'] for item in pending['banks']), 'snapshot credit conservation')
            check(pending['outstanding'] <= pending['capacity'], 'snapshot global capacity')
            for item in pending['banks']:
                check(item['outstanding'] <= item['capacity'] and item['outstanding'] ==
                      sum(flight['bank'] == item['bank'] for flight in pending['flights']), 'snapshot bank ownership')
            return pending
        def execute(name, config, workload=None, expected='PASS'):
            outcome = bank.run(config, output / name, binary, workload)
            check(outcome['status'] == expected, f'{name}: {outcome.get("error", outcome["status"])}')
            result['cases'].append({'name': name, 'expected_execution': expected, 'status': 'PASS'})
            return outcome
        baseline = execute('baseline', {'write_percent': 50})
        drained = snapshot('baseline')
        check(drained['outstanding'] == 0 and all(port['remaining'] == 0 and not port['queued']
              for port in drained['ports']), 'successful snapshot not drained')
        replay = execute('replay', {}, output / 'baseline/simulation.workload.trace')
        check(baseline['summary']['finish_cycle'] == replay['summary']['finish_cycle'] and
              baseline['summary']['memory_hash'] == replay['summary']['memory_hash'], 'replay behavior changed')
        burst_config = bank.load(bank.SYSTEM / 'configs/bursts.yaml')
        burst = execute('burst', burst_config)
        check(burst['summary']['accepted'] == 5 and burst['summary']['completed'] == 5 and
              burst['summary']['finish_cycle'] == 23, 'finite burst analytical drain time')
        plan = (output / 'burst/simulation.workload.trace').read_text().splitlines()[1:]
        check([int(row.split()[2]) for row in plan] == [0, 0, 10, 10, 20] and
              sum(len(row.split()[5]) // 2 for row in plan) == 80, 'burst release/partial tail/byte budget')
        observed = json.loads((output / 'burst/analysis.json').read_text())
        check([t['stages']['accept'] for t in observed['transactions']] == [0, 3000, 10000, 13000, 20000],
              'burst admission changed under finite credits')
        burst_replay = execute('burst_replay', dict(burst_config, burst_requests=1, burst_period_cycles=0),
                               output / 'burst/simulation.workload.trace')
        check(burst_replay['summary'] == burst['summary'], 'replay changed burst timing/data/counts')
        for mode in ('off', 'counters'):
            other = execute('burst_' + mode, dict(burst_config, observation=mode))
            check(other['summary']['finish_cycle'] == 23 and other['summary']['memory_hash'] == burst['summary']['memory_hash'],
                  'burst behavior depends on observation')
        execute('burst_phases', dict(burst_config, ports=2, phase_cycles=2))
        phased = [row.split() for row in (output / 'burst_phases/simulation.workload.trace').read_text().splitlines()[1:]]
        check([int(row[2]) for row in phased] == [0, 2, 0, 2, 10, 12, 10, 12, 20, 22] and
              [int(row[0]) for row in phased] == list(range(10)), 'per-source burst phase/ID plan')
        execute('burst_budget', dict(burst_config, max_cycles=8), expected='FAIL')
        pending_burst = snapshot('burst_budget')
        check(pending_burst['outstanding'] == 0 and pending_burst['ports'][0]['remaining'] == 3 and
              pending_burst['ports'][0]['earliest_cycle'] == 10, 'future burst lost on budget exhaustion')
        # Older resolved v1 files omit the additive burst fields and retain defaults.
        legacy = output / 'legacy-flat'
        legacy.mkdir()
        old_config = legacy / 'old.cfg'
        old_config.write_text(''.join(line + '\n' for line in (output / 'baseline/resolved.cfg').read_text().splitlines()
                                     if not line.startswith(('burst_requests=', 'burst_period_cycles='))))
        bank.command([binary, old_config, '-', legacy / 'simulation'], legacy / 'simulation.log')
        check(json.loads((legacy / 'simulation.summary.json').read_text()) == baseline['summary'], 'legacy flat config changed')
        result['cases'].append({'name': 'legacy_flat_burst_defaults', 'status': 'PASS'})
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
        stalled = snapshot('watchdog')
        check(stalled['cycle'] == 1 and stalled['period_ps'] == 1000 and
              stalled['flights'] == [{'id': 0, 'source': 0, 'bank': 0, 'accepted_cycle': 0,
                                     'service_ready_cycle': 8, 'response_ready_cycle': 10, 'stage': 'service'}],
              'service snapshot does not explain pending transaction')
        check(stalled['ports'][1]['queued'] and stalled['ports'][1]['next_id'] == 1,
              'queued arbitration loser missing')
        for mode in ('off', 'counters'):
            name = 'watchdog_' + mode
            execute(name, {'max_cycles': 1, 'observation': mode}, expected='FAIL')
            check(snapshot(name) == stalled, 'pending diagnosis depends on trace observation')
        # max_cycles is an execution budget, not a proof of deadlock. At cycle 2,
        # the service due at that boundary has not yet been processed.
        execute('watchdog_boundary', {'max_cycles': 2, 'latency': 2}, expected='FAIL')
        boundary = snapshot('watchdog_boundary')
        check(boundary['flights'][0]['service_ready_cycle'] == boundary['cycle'] and
              boundary['flights'][0]['stage'] == 'service', 'snapshot predicted unprocessed completion')
        execute('watchdog_response', {'max_cycles': 5, 'latency': 2, 'response_cycles': 3}, dependency, expected='FAIL')
        response = snapshot('watchdog_response')
        check(response['flights'] == [{'id': 100, 'source': 0, 'bank': 0, 'accepted_cycle': 2,
                                      'service_ready_cycle': 4, 'response_ready_cycle': 7, 'stage': 'response'}] and
              response['ports'][1]['waiting_on'] == [100] and not response['ports'][1]['queued'],
              'response/dependency waiting snapshot incorrect')
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
