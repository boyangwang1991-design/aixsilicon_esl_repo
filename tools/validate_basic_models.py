"""Build and test B0 and timer/IRQ models through source and relocatable installed consumers."""
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
                   'models/timer', 'models/irq_controller', 'common/systemc', 'cmake',
                   'contracts', 'examples/basic_system', 'examples/interrupt_system']:
        for p in sorted((ROOT / folder).rglob('*')):
            if p.is_file():
                result['source_sha256'][str(p.relative_to(ROOT))] = hashlib.sha256(p.read_bytes()).hexdigest()

    for filename in ['CMakeLists.txt', 'tools/validate_basic_models.py']:
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
        for example in ['basic_system', 'interrupt_system']:
            step = f'{label}-{example}'
            build = str(out / step)
            run(step + '-configure', [cmake, '-S', str(ROOT / 'examples' / example), '-B', build, *extra])
            run(step + '-build', [cmake, '--build', build, '-j', '4'])
            run(step + '-test', [ctest, '--test-dir', build, '--output-on-failure', '--no-tests=error', '--output-junit', 'results.xml'])

    try:
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
