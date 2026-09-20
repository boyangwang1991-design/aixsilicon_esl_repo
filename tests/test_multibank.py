"""Configuration and artifact checks; the SystemC suite owns simulation evidence."""
import copy
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import multibank as bank


def test_defaults_match_standalone_fixture():
    config, graph = bank.resolve({})
    fixture = dict(line.split('=', 1) for line in (bank.SYSTEM / 'configs/default.cfg').read_text().splitlines())
    assert fixture == {key: str(value) for key, value in config.items() if key != 'connections'}
    assert len(graph['components']) == config['ports'] + config['banks'] + 2
    assert bank.resolve(config)[0] == config


@pytest.mark.parametrize('config', [
    {'ports': True}, {'extra': 1}, {'banks': 3, 'mapping': 'xor', 'capacity': 3072},
    {'bytes': 12}, {'ports': 3}, {'stride': 3}, {'interval': 0},
    {'warmup_cycles': 10, 'max_cycles': 10}, {'ports': 64, 'requests': 100000},
    {'storage': 'python'}, {'observation': False}, {'seed': 2**64},
])
def test_invalid_config_rejected(config):
    with pytest.raises(ValueError):
        bank.resolve(config)


@pytest.mark.parametrize('mutation', ['missing', 'duplicate', 'direction', 'unknown', 'rewire'])
def test_connection_failures(mutation):
    config, _ = bank.resolve({})
    edges = config['connections']
    if mutation == 'missing':
        edges.pop()
    elif mutation == 'duplicate':
        edges.append(copy.deepcopy(edges[0]))
    elif mutation == 'direction':
        edges[0] = {'from': edges[0]['to'], 'to': edges[0]['from']}
    elif mutation == 'unknown':
        edges[0]['to'] = 'unknown.request'
    else:
        edges[0]['to'], edges[2]['to'] = edges[2]['to'], edges[0]['to']
    with pytest.raises(ValueError):
        bank.resolve(config)


def test_duplicate_yaml_keys(tmp_path):
    path = tmp_path / 'bad.yaml'
    path.write_text('ports: 2\nports: 4\n')
    with pytest.raises(ValueError):
        bank.load(path)


def test_failed_config_retained_and_not_overwritten(tmp_path):
    output = tmp_path / 'run'
    result = bank.run({'interval': 0}, output)
    assert result['status'] == 'FAIL'
    assert json.loads((output / 'run.json').read_text())['status'] == 'FAIL'
    assert (output / 'report.html').is_file()
    with pytest.raises(FileExistsError):
        bank.run({}, output)


def test_capabilities_match_schema():
    capabilities = json.loads((bank.SYSTEM / 'capabilities.json').read_text())
    schema = json.loads(bank.SCHEMA.read_text())
    for field in ('mapping', 'arbitration', 'storage'):
        assert capabilities[field] == schema['properties'][field]['enum']


def test_invalid_sweep_preserves_failure(tmp_path):
    output = tmp_path / 'sweep'
    result = bank.sweep({'base': {}, 'matrix': {}}, output)
    assert result['status'] == 'FAIL'
    assert json.loads((output / 'sweep.json').read_text())['status'] == 'FAIL'


def test_measurement_clipping_and_truncation(tmp_path):
    config, _ = bank.resolve({'bytes': 1, 'warmup_cycles': 1})
    path = tmp_path / 'events.csv'
    rows = ('0,1,0,port0,enqueue,ingress,0\n'
            '0,1,0,port0,wait_arbitration,bank0,0\n'
            '1000,1,0,port0,wait_bank_interval,bank0,0\n'
            '2000,1,0,port0,accept,bank0,0\n'
            '2000,1,0,port0,service,bank0,1\n'
            '4000,1,0,port0,write,bank0,1\n'
            '4000,1,0,port0,complete,bank0,1\n'
            '5000,1,0,port0,response_ready,return,0\n'
            '6000,1,0,port0,retire,return,1\n')
    path.write_text('# aix-esl-events-v1,tick_seconds=1e-12,dropped=0\n'
                    'tick,id,parent,source,phase,resource,value\n' + rows)
    summary = {'status': 'PASS', 'completed': 1, 'finish_cycle': 6,
               'measured_transactions': 1, 'measured_latency_cycles': 4}
    analysis = bank.analyze(path, config, summary)
    assert analysis['p95_ticks'] == 4000
    assert analysis['queue_occupied_ticks']['port0'] == 1000
    assert analysis['wait_request_cycles'] == {'wait_bank_interval': 1}
    summary['measured_latency_cycles'] = 3
    with pytest.raises(ValueError, match='measurement mismatch'):
        bank.analyze(path, config, summary)


def test_topology_diagram_comes_from_resolved_connections():
    _, graph = bank.resolve({'ports': 4, 'banks': 8})
    svg = bank.topology_svg(graph)
    for component in graph['components']:
        assert component['id'] in svg
    assert svg.count('marker-end="url(#arrow)"') == len(graph['connections'])


def test_malformed_yaml_run_is_archived(tmp_path):
    from argparse import Namespace
    config = tmp_path / 'bad.yaml'
    config.write_text('ports: 2\nports: 3\n')
    output = tmp_path / 'failed'
    args = Namespace(action='run', config=config, output=output, workload=None, executable=None, cmake='cmake', timeout=30)
    assert bank.main(args) == 1
    failure = json.loads((output / 'run.json').read_text())
    assert failure['status'] == 'FAIL' and failure['stage'] == 'load_configuration'
    assert (output / 'requested.yaml').read_text() == config.read_text()
