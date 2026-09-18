"""Executable contracts for the current SystemC manifests and explicit reference backend."""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

import yaml

BACKEND = 'legacy-python-mini-pipeline'
MODEL_ID = re.compile(r'^aixsilicon:esl:[a-z][a-z0-9_]*:\d+\.\d+\.\d+$')
NAME = re.compile(r'^[a-z][a-z0-9_]*$')
OWNERS = {'dma': {'dma_queue_depth', 'dma_bytes_per_tick'}, 'bmu': {'num_slots'},
          'compute': {'n_elements', 'compute_elements_per_tick'}}
DEFAULTS = {'dma_queue_depth': 4, 'dma_bytes_per_tick': 32, 'num_slots': 1,
            'n_elements': 16384, 'compute_elements_per_tick': 8}
CONNECTIONS = [{'from': 'dma', 'to': 'ext_mem', 'kind': 'memory'},
               {'from': 'dma', 'to': 'sram', 'kind': 'memory'},
               {'from': 'compute', 'to': 'sram', 'kind': 'memory'}]


def fields(obj, allowed, required=()):
    if not isinstance(obj, dict):
        raise ValueError('expected a mapping')
    unknown, missing = set(obj) - set(allowed), set(required) - set(obj)
    if unknown or missing:
        raise ValueError(f'unknown fields={sorted(unknown)}; missing fields={sorted(missing)}')


def load(path):
    data = yaml.safe_load(Path(path).read_text(encoding='utf-8'))
    if not isinstance(data, dict):
        raise ValueError(f'{path}: expected a YAML mapping')
    return data


def inside(root, relative):
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError(f'path escapes asset root: {relative}')
    return path


def validate_reference(system, backend=None):
    fields(system, {'schema_version', 'id', 'backend', 'instances', 'connections'},
           {'schema_version', 'id', 'instances', 'connections'})
    selected = backend or system.get('backend')
    if selected != BACKEND or system.get('backend', selected) != selected:
        raise ValueError(f'backend must explicitly be {BACKEND}; SystemC uses its CMake consumer')
    if system['schema_version'] != 1 or system['id'] != 'aixsilicon:esl:system:mini_pipeline:0.1.0':
        raise ValueError('unsupported system id/version for reference backend')
    instances = system['instances']
    if not isinstance(instances, dict) or set(instances) != set(OWNERS):
        raise ValueError('reference backend requires exactly dma, bmu and compute')
    if not isinstance(system['connections'], list) or sorted(json.dumps(c, sort_keys=True) for c in system['connections']) != sorted(json.dumps(c, sort_keys=True) for c in CONNECTIONS):
        raise ValueError('reference backend requires its declared fixed memory connections')
    params = dict(DEFAULTS)
    for name, spec in instances.items():
        fields(spec, {'model', 'params'}, {'model'})
        if spec['model'] != f'aixsilicon:esl:{name}:0.1.0':
            raise ValueError(f'unsupported reference model: instances.{name}.model')
        supplied = spec.get('params', {})
        fields(supplied, OWNERS[name])
        params.update(supplied)
    for name, value in params.items():
        if type(value) is not int or value <= 0:
            raise ValueError(f'{name} must be a positive integer')
    if params['num_slots'] > 8:
        raise ValueError('num_slots exceeds 8 KiB reference SRAM capacity')
    if params['n_elements'] > 32768 or params['n_elements'] % 256:
        raise ValueError('n_elements must be a multiple of 256 in 256..32768')
    return params


def validate_model(path):
    path = Path(path)
    model = load(path)
    fields(model, {'schema_version', 'id', 'factory', 'model_kinds', 'profiles', 'ports',
                   'parameters', 'assumptions', 'build'},
           {'schema_version', 'id', 'factory', 'model_kinds', 'profiles', 'ports', 'parameters', 'build'})
    if model['schema_version'] != 1 or not MODEL_ID.fullmatch(model['id']):
        raise ValueError(f'{path}: invalid model schema/id')
    name = model['id'].split(':')[2]
    if model['factory'] != f'aix::esl::{name}::Model':
        raise ValueError('factory must match public model namespace')
    if not model['model_kinds'] or not set(model['model_kinds']) <= {'behavioral', 'performance', 'software_visible', 'microarchitecture'}:
        raise ValueError('unsupported model_kinds')
    if not isinstance(model['profiles'], dict) or not model['profiles']:
        raise ValueError('profiles must be a nonempty mapping')
    for profile in model['profiles'].values():
        fields(profile, {'timing_model', 'data_modes', 'interface_mode', 'transport', 'capabilities'},
               {'timing_model', 'data_modes', 'interface_mode', 'transport', 'capabilities'})
        if profile['timing_model'] not in {'untimed', 'annotated', 'resource_contention', 'cycle_accurate'}:
            raise ValueError('unsupported timing_model')
        if not profile['data_modes'] or not set(profile['data_modes']) <= {'full_data', 'traffic_only'}:
            raise ValueError('unsupported data_modes')
        if profile['transport'] not in {'LT', 'AT', 'direct'}:
            raise ValueError('unsupported transport')
    if not isinstance(model['ports'], dict) or not model['ports']:
        raise ValueError('ports must be a nonempty mapping')
    for port in model['ports'].values():
        fields(port, {'direction', 'type', 'transport', 'bus_width', 'binding', 'active', 'count_parameter'},
               {'direction', 'type', 'binding'})
        if port['direction'] not in {'target', 'initiator', 'input', 'output'}:
            raise ValueError('invalid port direction')
        if port['binding'] not in {'one_or_more', 'exactly_one', 'required', 'all_required', 'optional'}:
            raise ValueError('invalid port binding')
        if port['type'] == 'tlm_generic_payload' and (port.get('transport') not in {'LT', 'AT'}
                or type(port.get('bus_width')) is not int or port['bus_width'] <= 0):
            raise ValueError('TLM port requires transport and positive bus_width')
        if port.get('count_parameter') and port['count_parameter'] not in model['parameters']:
            raise ValueError('unknown port count_parameter')
    for name_, parameter in model['parameters'].items():
        fields(parameter, {'type', 'default', 'min', 'max', 'required', 'constraint'}, {'type'})
        if parameter['type'] not in {'integer', 'boolean', 'time', 'byte_array', 'region_array'}:
            raise ValueError(f'{name_}: unknown parameter type')
        if parameter.get('constraint') not in {None, 'positive', 'forced_true'}:
            raise ValueError(f'{name_}: unknown constraint')
        if 'default' not in parameter:
            if parameter.get('required') is not True:
                raise ValueError(f'{name_}: default or required needed')
            continue
        value = parameter['default']
        if parameter['type'] == 'integer':
            if type(value) is not int or value < parameter.get('min', value) or value > parameter.get('max', value):
                raise ValueError(f'{name_}: invalid integer default')
        elif parameter['type'] == 'boolean' and type(value) is not bool:
            raise ValueError(f'{name_}: invalid boolean default')
        elif parameter['type'] == 'time' and not re.fullmatch(r'\d+(?:\.\d+)?\s+(?:fs|ps|ns|us|ms|s)', str(value)):
            raise ValueError(f'{name_}: invalid time default')
        elif parameter['type'] == 'byte_array' and (not isinstance(value, list) or any(type(v) is not int or not 0 <= v <= 255 for v in value)):
            raise ValueError(f'{name_}: invalid byte array')
        if parameter.get('constraint') == 'forced_true' and value is not True:
            raise ValueError(f'{name_}: must be true')
        if parameter.get('constraint') == 'positive' and float(str(value).split()[0]) <= 0:
            raise ValueError(f'{name_}: must be positive')
    build = model['build']
    fields(build, {'backend', 'package', 'target', 'cxx_standard', 'entry'},
           {'backend', 'package', 'target', 'cxx_standard', 'entry'})
    if build['backend'] != 'cmake' or build['target'] != f'aix::esl::{name}' or build['cxx_standard'] != 17:
        raise ValueError('unsupported build contract')
    if not re.fullmatch(r'AixEsl[A-Za-z0-9]+', build['package']):
        raise ValueError('invalid CMake package name')
    for relative in [build['entry'], f'systemc/include/{name}/model.hpp', f'systemc/include/{name}/config.hpp',
                     'systemc/src/model.cpp', 'README.md', 'docs/design.md', 'docs/integration.md', 'docs/verification.md']:
        if not inside(path.parent, relative).is_file():
            raise ValueError(f'missing model delivery file: {relative}')
    return model


def validate_common(path):
    path = Path(path)
    data = load(path)
    fields(data, {'schema_version', 'id', 'build', 'headers', 'consumers'},
           {'schema_version', 'id', 'build', 'headers', 'consumers'})
    if data['schema_version'] != 1 or data['id'] != 'aixsilicon:esl:common:0.1.0':
        raise ValueError('unsupported common package schema/id')
    expected = {'backend': 'cmake', 'package': 'AixEslCommon', 'target': 'aix::esl::common',
                'cxx_standard': 17, 'entry': 'CMakeLists.txt'}
    if data['build'] != expected:
        raise ValueError('invalid common package build contract')
    headers = data['headers']
    if not isinstance(headers, list) or not headers or len(set(headers)) != len(headers):
        raise ValueError('common headers must be a nonempty unique list')
    if any(not isinstance(h, str) or not h.startswith('systemc/include/aix/esl/') or not h.endswith('.hpp')
           for h in headers):
        raise ValueError('invalid common public header path')
    for relative in [*headers, 'CMakeLists.txt', 'README.md', 'docs/design.md',
                     'docs/integration.md', 'docs/verification.md']:
        if not inside(path.parent, relative).is_file():
            raise ValueError(f'missing common delivery file: {relative}')
    if not isinstance(data['consumers'], list) or len(set(data['consumers'])) < 2:
        raise ValueError('common package requires multiple actual consumers')
    for relative in data['consumers']:
        if not inside(path.parent.parent, relative).is_dir():
            raise ValueError(f'missing common consumer: {relative}')
    return data


def registry(root):
    data = load(root / 'registry.yaml')
    fields(data, {'schema_version', 'assets'}, {'schema_version', 'assets'})
    if data['schema_version'] != 'aixsilicon.esl-registry/v1' or not isinstance(data['assets'], list):
        raise ValueError('unsupported registry schema')
    ids = set()
    for asset in data['assets']:
        fields(asset, {'id', 'kind', 'path', 'status', 'description', 'evidence', 'aliases'}, {'id', 'kind', 'path', 'status'})
        if asset['kind'] not in {'common', 'model', 'example', 'template'}:
            raise ValueError('unknown asset kind')
        aliases = asset.get('aliases', [])
        if not isinstance(aliases, list) or any(not isinstance(a, str) or not re.fullmatch(
                r'aixsilicon:esl:[a-z][a-z0-9_]*:[a-z][a-z0-9_]*:\d+\.\d+\.\d+', a) for a in aliases):
            raise ValueError('aliases must be legacy five-part ESL IDs')
        names = [asset['id'], *aliases]
        if len(set(names)) != len(names) or ids.intersection(names) or asset['status'] not in {'planned', 'available'}:
            raise ValueError('duplicate id or invalid registry status')
        ids.update(names)
        path = inside(root, asset['path'])
        if not path.is_dir():
            raise ValueError(f'missing registered directory: {path}')
        if asset['status'] != 'available':
            continue
        if not MODEL_ID.fullmatch(asset['id']):
            raise ValueError('available assets require four-part IDs')
        if asset['kind'] == 'model' and validate_model(path / 'model.yaml')['id'] != asset['id']:
            raise ValueError('registry/manifest id mismatch')
        if asset['kind'] == 'common' and validate_common(path / 'common.yaml')['id'] != asset['id']:
            raise ValueError('registry/common manifest id mismatch')
        if asset['kind'] == 'example' and not all((path / f).is_file() for f in ['README.md', 'CMakeLists.txt']):
            raise ValueError('available example lacks README or CMake entry')
        if asset['kind'] == 'template':
            meta = load(path / 'template.yaml')
            fields(meta, {'id', 'kind', 'files', 'support_files'}, {'id', 'kind', 'files', 'support_files'})
            if meta['id'] != asset['id'] or not meta['files']:
                raise ValueError('invalid available template manifest')
            for relative in meta['files']:
                if not inside(path, relative).is_file():
                    raise ValueError(f'missing template file: {relative}')
            for relative in meta['support_files'].values():
                if not inside(root, relative).is_file():
                    raise ValueError(f'missing template support file: {relative}')
    return data['assets']


def evidence_status(root, asset):
    if not asset.get('evidence'):
        return 'NOT_LINKED'
    path = inside(root, asset['evidence'])
    if not path.is_file():
        return 'UNAVAILABLE'
    data = json.loads(path.read_text())
    hashes = data.get('source_sha256', {})
    if data.get('status') != 'PASS' or not hashes:
        return 'INVALID'
    if not any(name.startswith(asset['path'] + '/') for name in hashes):
        return 'UNCOVERED'
    for relative, digest in hashes.items():
        source = inside(root, relative)
        if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            return 'STALE'
    return 'CURRENT'
