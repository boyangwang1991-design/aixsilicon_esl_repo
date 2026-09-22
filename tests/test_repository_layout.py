"""Integration checks for the target/reference directory split."""
import json
import os
import subprocess
import sys
from pathlib import Path

import yaml
import pytest

ROOT = Path(__file__).resolve().parents[1]


def test_common_layer_ownership_and_hash_coverage():
    sys.path.insert(0, str(ROOT / 'tools'))
    import esl_contracts
    import multibank
    import npu_explore
    manifest = esl_contracts.validate_common(ROOT / 'common/common.yaml')
    assert manifest['schema_version'] == 2
    assert not list((ROOT / 'common/systemc').rglob('*.hpp'))
    actual = set()
    for layer in ('primitives', 'infrastructure', 'adapters', 'services', 'workloads', 'verification'):
        actual.update(str(p.relative_to(ROOT)) for p in (ROOT / layer).rglob('*.hpp') if p.name != 'mmio32.hpp')
    assert actual == set(manifest['headers'])
    assert set(manifest['headers']) <= multibank.source_hashes().keys()
    assert 'services/systemc/include/aix/esl/byte_store.hpp' in npu_explore.source_hashes()


@pytest.mark.parametrize('bad', [
    '../escape.hpp', 'services/systemc/include/aix/esl/../../escape.hpp',
    'services/systemc/include/aix/esl/mmio32.hpp',
])
def test_common_manifest_rejects_invalid_header_paths(tmp_path, bad):
    sys.path.insert(0, str(ROOT / 'tools'))
    import esl_contracts
    data = yaml.safe_load((ROOT / 'common/common.yaml').read_text())
    data['headers'][0] = bad
    path = tmp_path / 'common.yaml'
    path.write_text(yaml.safe_dump(data))
    with pytest.raises(ValueError, match='invalid common public header path'):
        esl_contracts.validate_common(path)


def test_registry_paths_and_target_namespace():
    assets = yaml.safe_load((ROOT / 'registry.yaml').read_text())['assets']
    for asset in assets:
        assert (ROOT / asset['path']).is_dir(), asset['id']
        if asset['kind'] == 'model' and asset['status'] == 'available':
            manifest = yaml.safe_load((ROOT / asset['path'] / 'model.yaml').read_text())
            assert manifest['id'] == asset['id']
            assert manifest['build']['backend'] == 'cmake'
    assert not list((ROOT / 'models').glob('*.py'))
    assert not (ROOT / 'examples/mini_pipeline').exists()
    assert not (ROOT / 'examples/min_systemc').exists()
    assert (ROOT / 'tests/environment/systemc/CMakeLists.txt').is_file()


def test_cli_legacy_import_from_external_directory(tmp_path):
    # Calling the actual command must work without a caller-supplied PYTHONPATH.
    config = yaml.safe_load((ROOT / 'reference/legacy_python/examples/mini_pipeline/system.yaml').read_text())
    path = tmp_path / 'system.yaml'
    path.write_text(yaml.safe_dump(config))
    env = dict(os.environ)
    env.pop('PYTHONPATH', None)
    result = subprocess.run([sys.executable, str(ROOT / 'tools/esl_cli.py'), 'run', str(path)],
                            cwd=tmp_path, env=env, capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stderr
    response = json.loads(result.stdout)
    assert response.get('overall_status', response.get('status')) in ('PASS', 'OK'), response
