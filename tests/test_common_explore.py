"""Offline event protocol checks; SystemC CTest owns model behavior evidence."""
import importlib.util
from pathlib import Path

import pytest

SPEC = importlib.util.spec_from_file_location(
    'common_explore', Path(__file__).resolve().parents[1] / 'tools/common_explore.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def trace(tmp_path, rows, dropped=0):
    path = tmp_path / 'events.csv'
    path.write_text(f'# aix-esl-events-v1,tick_seconds=1e-12,dropped={dropped}\n'
                    'tick,id,parent,source,phase,resource,value\n' + rows)
    return path


GOOD = ('0,1,0,p0,accept,bank0,128\n'
        '0,1,0,p0,service,bank0,16\n'
        '8,1,0,p0,complete,bank0,16\n'
        '10,1,0,p0,retire,response,0\n')


def test_independent_latency_and_occupancy(tmp_path):
    result = MODULE.analyze(trace(tmp_path, GOOD))
    assert result['p95_ticks'] == result['p99_ticks'] == 10
    assert sum(result['bank_average_active']['bank0']) == 8
    assert result['mapping'] == [{'id': 1, 'address': 128, 'bank': 'bank0'}]


@pytest.mark.parametrize('rows', [GOOD + '10,1,0,p0,retire,response,0\n',
                                 GOOD.replace('8,1,0', '0,2,0'),
                                 GOOD.rsplit('10,', 1)[0]])
def test_reject_invalid_conservation(tmp_path, rows):
    with pytest.raises(ValueError):
        MODULE.analyze(trace(tmp_path, rows))


def test_truncation_cannot_claim_latency(tmp_path):
    result = MODULE.analyze(trace(tmp_path, GOOD, dropped=1))
    assert not result['complete_trace'] and result['p95_ticks'] is None
