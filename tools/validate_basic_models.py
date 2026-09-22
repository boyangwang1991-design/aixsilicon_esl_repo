"""Build and test B0/B1/B2 models through source and relocatable installed consumers."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path

from esl_contracts import validate_common

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    out = (args.output or ROOT / 'runs' / time.strftime('basic-models-%Y%m%d-%H%M%S')).resolve()
    out.mkdir(parents=True, exist_ok=False)
    cmake = shutil.which(args.cmake)
    if not cmake:
        parser.error('CMake executable not found')
    ctest = str(Path(cmake).with_name('ctest'))
    result = {'status': 'RUNNING', 'steps': [], 'source_sha256': {}, 'test_executions': 0, 'test_cases': []}
    for folder in ['models/ram', 'models/rom', 'models/host_master', 'models/tlm_bus',
                   'models/timer', 'models/irq_controller', 'models/uart', 'models/gpio', 'models/dma',
                   'common', 'primitives', 'infrastructure', 'adapters', 'services', 'workloads', 'verification',
                   'cmake', 'contracts', 'examples/basic_system',
                   'examples/interrupt_system', 'examples/peripheral_system', 'examples/dma_system',
                   'examples/common_primitives', 'systems/multibank']:
        for p in sorted((ROOT / folder).rglob('*')):
            if p.is_file():
                result['source_sha256'][str(p.relative_to(ROOT))] = hashlib.sha256(p.read_bytes()).hexdigest()

    for filename in ['CMakeLists.txt', 'tools/validate_basic_models.py', 'tools/esl_contracts.py',
                     'tools/esl_cli.py', 'tools/multibank.py', 'tools/common_explore.py', 'tools/validate_multibank.py']:
        result['source_sha256'][filename] = hashlib.sha256((ROOT / filename).read_bytes()).hexdigest()

    def run(label: str, command: list[str]) -> None:
        log = out / f'{label}.log'
        with log.open('w') as stream:
            process = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        result['steps'].append({'name': label, 'command': command, 'returncode': process.returncode,
                                'log': log.name})
        if process.returncode:
            raise RuntimeError(f'{label} failed: {log}')
        if command[0] == ctest:
            build_dir = Path(command[command.index('--test-dir') + 1])
            cases = [case.attrib['name'] for case in ET.parse(build_dir / 'results.xml').iter('testcase')]
            result['test_executions'] += len(cases)
            result['test_cases'] = sorted(set(result['test_cases']) | set(cases))

    def consumer(label: str, extra: list[str]) -> None:
        for example in ['common_primitives', 'basic_system', 'interrupt_system', 'peripheral_system', 'dma_system', 'multibank']:
            step = f'{label}-{example}'
            build = str(out / step)
            source = ROOT / ('systems' if example == 'multibank' else 'examples') / example
            run(step + '-configure', [cmake, '-S', str(source), '-B', build, *extra])
            run(step + '-build', [cmake, '--build', build, '-j', '4'])
            run(step + '-test', [ctest, '--test-dir', build, '--output-on-failure', '--no-tests=error', '--output-junit', 'results.xml'])

    try:
        common = validate_common(ROOT / 'common/common.yaml')
        run('cmake-version', [cmake, '--version'])
        build = str(out / 'producer')
        prefix = out / 'prefix'
        run('producer-configure', [cmake, '-S', str(ROOT), '-B', build,
                                  '-DESL_ENABLE_SYSTEMC=ON', '-DESL_RAM_BUILD_TESTS=ON',
                                  f'-DCMAKE_INSTALL_PREFIX={prefix}'])
        run('producer-build', [cmake, '--build', build, '-j', '4'])
        run('ram-test', [ctest, '--test-dir', str(out / 'producer/models/ram'), '--output-on-failure', '--no-tests=error', '--output-junit', 'results.xml'])
        consumer('source', [f'-DESL_MODELS_SOURCE_DIR={ROOT}'])
        run('install', [cmake, '--install', build])
        consumer('installed', [f'-DCMAKE_PREFIX_PATH={prefix}'])
        relocated = out / 'relocated-prefix'
        prefix.rename(relocated)
        consumer('relocated', [f'-DCMAKE_PREFIX_PATH={relocated}'])
        for path in relocated.rglob('*.cmake'):
            if str(ROOT) in path.read_text():
                raise RuntimeError(f'source path leaked into exported package: {path}')
        docroot = relocated / 'share/aix-esl'
        missing_headers = [h for h in common['headers'] if not
                           (relocated / 'include/aix/esl' / Path(h).name).is_file()]
        expected_headers = {Path(h).name for h in common['headers']}
        installed_headers = {h.name for h in (relocated / 'include/aix/esl').glob('*.hpp')}
        private_exported = (relocated / 'include/aix/esl/mmio32.hpp').exists()
        result['common_package'] = {'missing_public_headers': missing_headers,
                                    'private_mmio_exported': private_exported}
        if missing_headers or private_exported or installed_headers != expected_headers:
            raise RuntimeError('common installed public/private header contract mismatch')
        broken = []
        for doc in docroot.rglob('*.md'):
            for target in re.findall(r'(?<!!)\[[^\]]+\]\(([^)\s]+)\)', doc.read_text()):
                target = target.split('#', 1)[0]
                if '://' in target or not target.endswith('.md'):
                    continue
                destination = (doc.parent / target).resolve()
                if not destination.is_relative_to(docroot.resolve()) or not destination.is_file():
                    broken.append({'document': str(doc.relative_to(docroot)), 'target': target})
        result['installed_documentation'] = {'broken_markdown_links': broken,
            'mmio_contract_present': (docroot / 'contracts/mmio32.md').is_file()}
        if broken or not result['installed_documentation']['mmio_contract_present']:
            raise RuntimeError('installed documentation incomplete; see checks.json')
        result['unique_test_cases'] = len(result['test_cases'])
        if any(hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != digest
               for path, digest in result['source_sha256'].items()):
            raise RuntimeError('source changed during verification')
        result['status'] = 'PASS'
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as exc:
        result['status'] = 'FAIL'
        result['error'] = str(exc)
    finally:
        (out / 'checks.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'status': result['status'], 'evidence': str(out / 'checks.json')}))
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
