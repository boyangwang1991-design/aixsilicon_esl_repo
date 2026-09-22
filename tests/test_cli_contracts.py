"""Wrong-object execution, failure semantics, template atomicity and manifest rejection."""
import json
import shutil
import sys
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import esl_cli as cli  # noqa: E402
import esl_contracts as contracts  # noqa: E402


@pytest.fixture
def system():
    return contracts.load(ROOT / 'reference/legacy_python/examples/mini_pipeline/system.yaml')


@pytest.mark.parametrize('fault', ['unknown_model', 'connections', 'params', 'id', 'backend', 'missing_backend', 'bounds'])
def test_reject_before_execution(system, fault, monkeypatch, tmp_path, capsys):
    if fault == 'unknown_model':
        system['instances']['dma']['model'] = 'aixsilicon:esl:missing:0.1.0'
    elif fault == 'connections':
        system['connections'][0]['to'] = 'missing.port'
    elif fault == 'params':
        system['instances']['dma']['params']['ignored_typo'] = 4
    elif fault == 'id':
        system['id'] = 'aixsilicon:esl:other:0.1.0'
    elif fault == 'backend':
        system['backend'] = 'systemc'
    elif fault == 'missing_backend':
        system.pop('backend')
    else:
        system['instances']['bmu']['params']['num_slots'] = 9
    path = tmp_path / 'system.yaml'
    path.write_text(yaml.safe_dump(system))
    def unexpected(**_):
        pytest.fail('invalid configuration reached backend')
    monkeypatch.setattr(cli, '_build_mini_pipeline', unexpected)
    assert cli.main(['run', str(path)]) != 0
    assert json.loads(capsys.readouterr().out)['status'] == 'FAIL'


def test_missing_file_nonzero(tmp_path, capsys):
    assert cli.main(['run', str(tmp_path / 'missing.yaml')]) != 0
    assert json.loads(capsys.readouterr().out)['status'] == 'FAIL'


def test_connection_mapping_and_list_order_are_irrelevant(system):
    expected = contracts.validate_reference(system)
    system['connections'] = [dict(reversed(list(c.items()))) for c in reversed(system['connections'])]
    assert contracts.validate_reference(system) == expected


def test_real_reference_oracle_and_timing(system):
    system['instances']['compute']['params']['n_elements'] = 256
    result = cli._execute_reference(system, None)
    assert result['status'] == 'PASS' and result['total_ticks'] == 76
    assert result['scope'] == 'legacy_reference' and result['target_validation'] == 'NOT_RUN'


def test_independent_oracle_detects_bad_compute(system, monkeypatch):
    system['instances']['compute']['params']['n_elements'] = 256
    original = cli._build_mini_pipeline
    def corrupted(**params):
        model = original(**params)
        run = model.run
        def corrupt_run():
            ticks = run()
            model.ext_mem.write(0x10000, b'\0\0')
            return ticks
        model.run = corrupt_run
        return model
    monkeypatch.setattr(cli, '_build_mini_pipeline', corrupted)
    result = cli._execute_reference(system, None)
    assert result['status'] == 'FAIL' and result['data_ok'] is False


def template_fixture(tmp_path, monkeypatch, status='available'):
    shutil.copytree(ROOT / 'templates/register_target', tmp_path / 'templates/register_target')
    shutil.copytree(ROOT / 'cmake', tmp_path / 'cmake')
    shutil.copytree(ROOT / 'contracts', tmp_path / 'contracts')
    (tmp_path / 'registry.yaml').write_text(yaml.safe_dump({'schema_version': 'aixsilicon.esl-registry/v1', 'assets': [
        {'id': 'aixsilicon:esl:register_target:0.1.0', 'kind': 'template', 'path': 'templates/register_target', 'status': status}]}))
    monkeypatch.setattr(cli, 'REPO_ROOT', tmp_path)


@pytest.mark.parametrize('fault', ['planned', 'missing_file', 'path_escape', 'kind'])
def test_template_rejects_without_partial_directory(tmp_path, monkeypatch, fault):
    template_fixture(tmp_path, monkeypatch, 'planned' if fault == 'planned' else 'available')
    if fault == 'missing_file':
        (tmp_path / 'templates/register_target/model.yaml').unlink()
    with pytest.raises((ValueError, OSError)):
        cli._render_template('register_target', '../escape' if fault == 'path_escape' else 'demo',
                             'system' if fault == 'kind' else 'model')
    assert not (tmp_path / 'models').exists()


def test_generated_model_has_library_contract(tmp_path, monkeypatch):
    template_fixture(tmp_path, monkeypatch)
    target = cli._render_template('register_target', 'demo', 'model')
    assert contracts.validate_model(target / 'model.yaml')['build']['target'] == 'aix::esl::demo'
    assert not (target / '__init__.py').exists()
    with pytest.raises(FileExistsError):
        cli._render_template('register_target', 'demo', 'model')


@pytest.mark.parametrize('fault', ['unknown_field', 'profile', 'port', 'parameter', 'missing_header'])
def test_manifest_rejects_invalid_delivery(tmp_path, fault):
    shutil.copytree(ROOT / 'models/ram', tmp_path / 'ram')
    path = tmp_path / 'ram/model.yaml'
    model = yaml.safe_load(path.read_text())
    if fault == 'unknown_field':
        model['silently_ignored'] = True
    elif fault == 'profile':
        model['profiles']['functional']['timing_model'] = 'magic'
    elif fault == 'port':
        model['ports']['memory']['direction'] = 'wrong'
    elif fault == 'parameter':
        model['parameters']['capacity_bytes']['default'] = 0
    else:
        (tmp_path / 'ram/systemc/include/ram/model.hpp').unlink()
    path.write_text(yaml.safe_dump(model))
    with pytest.raises(ValueError):
        contracts.validate_model(path)


def test_empty_comparison_is_failure(tmp_path, capsys):
    path = tmp_path / 'empty.json'
    path.write_text('{}')
    assert cli.main(['compare', str(path), str(path)]) != 0
    assert json.loads(capsys.readouterr().out)['status'] == 'FAIL'


def test_cartesian_sweep_and_failed_point(system, tmp_path, capsys):
    system['instances']['compute']['params']['n_elements'] = 256
    (tmp_path / 'system.yaml').write_text(yaml.safe_dump(system))
    path = tmp_path / 'experiment.yaml'
    experiment = {'schema_version': 1, 'system': 'system.yaml',
                  'sweep': {'num_slots': [1, 2], 'dma_queue_depth': [1, 0]}}
    path.write_text(yaml.safe_dump(experiment))
    assert cli.main(['sweep', str(path)]) != 0
    result = json.loads(capsys.readouterr().out)
    assert result['total'] == 4
    assert [p['status'] for p in result['points']].count('FAIL') == 2
    experiment.update(mode='zip', sweep={'num_slots': [1, 2], 'dma_queue_depth': [1]})
    path.write_text(yaml.safe_dump(experiment))
    assert cli.main(['sweep', str(path)]) != 0


def test_evidence_hash_change_is_not_current(tmp_path):
    import hashlib
    source = tmp_path / 'models/demo/model.cpp'
    source.parent.mkdir(parents=True)
    source.write_text('first revision')
    evidence = tmp_path / 'evidence.json'
    evidence.write_text(json.dumps({'status': 'PASS', 'source_sha256': {
        'models/demo/model.cpp': hashlib.sha256(source.read_bytes()).hexdigest()}}))
    asset = {'path': 'models/demo', 'evidence': 'evidence.json'}
    assert contracts.evidence_status(tmp_path, asset) == 'CURRENT'
    source.write_text('changed revision')
    assert contracts.evidence_status(tmp_path, asset) == 'STALE'
    evidence.unlink()
    assert contracts.evidence_status(tmp_path, asset) == 'UNAVAILABLE'


def test_bad_service_time_fails_sanity(system, monkeypatch):
    system['instances']['compute']['params']['n_elements'] = 256
    original = cli._build_mini_pipeline
    def impossible(**params):
        model = original(**params)
        run = model.run
        def run_zero_ticks():
            run()
            return 0
        model.run = run_zero_ticks
        return model
    monkeypatch.setattr(cli, '_build_mini_pipeline', impossible)
    result = cli._execute_reference(system, None)
    assert result['data_ok'] is True and result['status'] == 'FAIL'
    assert result['checks'][-1]['status'] == 'FAIL'


def test_legacy_alias_resolves_discovery_only(tmp_path, monkeypatch, capsys):
    template_fixture(tmp_path, monkeypatch)
    path = tmp_path / 'registry.yaml'
    data = yaml.safe_load(path.read_text())
    alias = 'aixsilicon:esl:template:register_target:0.1.0'
    data['assets'][0]['aliases'] = [alias]
    path.write_text(yaml.safe_dump(data))
    assert cli.main(['inspect', '--id', alias]) == 0
    result = json.loads(capsys.readouterr().out)
    assert result['resolved_id'] == data['assets'][0]['id']
    assert result['available'][0]['id'] == data['assets'][0]['id']
    assert cli.main(['inspect', '--id', 'aixsilicon:esl:missing:0.1.0']) != 0


@pytest.mark.parametrize('fault', ['duplicate_alias', 'invalid_alias', 'collision'])
def test_registry_rejects_ambiguous_alias(tmp_path, monkeypatch, fault):
    template_fixture(tmp_path, monkeypatch)
    path = tmp_path / 'registry.yaml'
    data = yaml.safe_load(path.read_text())
    alias = 'aixsilicon:esl:template:register_target:0.1.0'
    data['assets'][0]['aliases'] = [alias]
    if fault == 'duplicate_alias':
        data['assets'][0]['aliases'].append(alias)
    elif fault == 'invalid_alias':
        data['assets'][0]['aliases'] = ['not-an-id']
    else:
        data['assets'].append({'id': alias, 'kind': 'template', 'path': 'templates/register_target', 'status': 'planned'})
    path.write_text(yaml.safe_dump(data))
    with pytest.raises(ValueError):
        contracts.registry(tmp_path)


@pytest.mark.parametrize('fault', ['missing_header', 'wrong_target', 'missing_consumer'])
def test_common_delivery_requires_public_api_and_consumers(tmp_path, fault):
    source = ROOT / 'common'
    shutil.copytree(source, tmp_path / 'common')
    path = tmp_path / 'common/common.yaml'
    data = yaml.safe_load(path.read_text())
    for header in data['headers']:
        destination = tmp_path / header
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / header, destination)
    for consumer in data['consumers']:
        (tmp_path / consumer).mkdir(parents=True, exist_ok=True)
    if fault == 'missing_header':
        (tmp_path / data['headers'][0]).unlink()
    elif fault == 'wrong_target':
        data['build']['target'] = 'aix::esl::imaginary'
    else:
        data['consumers'].append('models/missing')
    path.write_text(yaml.safe_dump(data))
    with pytest.raises(ValueError):
        contracts.validate_common(path)
