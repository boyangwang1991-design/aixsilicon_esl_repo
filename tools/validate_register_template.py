"""Validate a candidate template in an isolated registry before making it available."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    out = (args.output or ROOT / 'runs' / time.strftime('register-template-%Y%m%d-%H%M%S')).resolve()
    out.mkdir(parents=True, exist_ok=False)
    result = {'status': 'RUNNING', 'scope': 'candidate_template_fixture', 'steps': [], 'source_sha256': {}}
    cmake = shutil.which(args.cmake)
    if not cmake:
        parser.error('CMake not found')
    ctest = str(Path(cmake).with_name('ctest'))

    def run(name, command):
        with (out / f'{name}.log').open('w') as log:
            code = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=180).returncode
        result['steps'].append({'name': name, 'command': command, 'returncode': code})
        if code:
            raise RuntimeError(f'{name} failed')

    try:
        fixture = out / 'fixture'
        for folder in ['templates/register_target', 'cmake', 'contracts']:
            shutil.copytree(ROOT / folder, fixture / folder)
        (fixture / 'tools').mkdir()
        for filename in ['esl_cli.py', 'esl_contracts.py']:
            shutil.copy2(ROOT / 'tools' / filename, fixture / 'tools' / filename)
        for path in fixture.rglob('*'):
            if path.is_file():
                result['source_sha256'][str(path.relative_to(fixture))] = hashlib.sha256(path.read_bytes()).hexdigest()
        (fixture / 'registry.yaml').write_text(yaml.safe_dump({'schema_version': 'aixsilicon.esl-registry/v1', 'assets': [
            {'id': 'aixsilicon:esl:register_target:0.1.0', 'kind': 'template', 'path': 'templates/register_target', 'status': 'available'}]}))
        generated = out / 'generated'
        run('generate', [sys.executable, str(fixture / 'tools/esl_cli.py'), 'new', 'model', 'sample_register', '--output', str(generated)])
        consumer = out / 'consumer'
        shutil.copytree(generated / 'examples/integration', consumer)
        prefix = out / 'prefix'
        run('producer-configure', [cmake, '-S', str(generated), '-B', str(out / 'producer'), f'-DCMAKE_INSTALL_PREFIX={prefix}'])
        run('producer-build', [cmake, '--build', str(out / 'producer'), '-j', '4'])
        for mode in ['source', 'installed', 'relocated']:
            if mode == 'installed':
                run('install', [cmake, '--install', str(out / 'producer')])
                generated.rename(out / 'hidden-generated-source')
            if mode == 'relocated':
                moved = out / 'relocated-prefix'
                prefix.rename(moved)
                prefix = moved
            extra = f'-DESL_MODEL_SOURCE_DIR={generated}' if mode == 'source' else f'-DCMAKE_PREFIX_PATH={prefix}'
            build = str(out / mode)
            run(mode + '-configure', [cmake, '-S', str(consumer), '-B', build, extra])
            run(mode + '-build', [cmake, '--build', build, '-j', '4'])
            run(mode + '-test', [ctest, '--test-dir', build, '--output-on-failure', '--no-tests=error'])
        result['status'] = 'PASS'
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as exc:
        result.update(status='FAIL', error=str(exc))
    finally:
        (out / 'checks.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'status': result['status'], 'evidence': str(out / 'checks.json')}))
    return int(result['status'] != 'PASS')


if __name__ == '__main__':
    raise SystemExit(main())
