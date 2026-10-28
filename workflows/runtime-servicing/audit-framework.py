"""Check the serviced framework's complete manifest and real compiler inputs.

This proves source/output provenance for the requested stage. It does not prove
runtime behavior, binary compatibility, or that a distributable is complete.
"""
from pathlib import Path
import argparse
import hashlib
import json
import posixpath
import re
import subprocess
import tarfile
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('root', type=Path)
parser.add_argument('--references-only', action='store_true')
args = parser.parse_args()
root = args.root.resolve()
source = root / 'm/src/runtime-8.0.31-native'
inputs = json.loads((root / 'source-inputs.json').read_text())
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()

def norm(value):
    return posixpath.normpath(value.replace('\\', '/'))

native_root = norm(subprocess.check_output(['cygpath', '-am', str(root)], text=True).strip())

def local(value):
    value = norm(value)
    assert value.lower().startswith(native_root.lower() + '/'), value
    return root / value[len(native_root) + 1:]

def compiler_inputs(rsp):
    original, generated, references, resources = {}, {}, {}, {}
    for line in rsp.read_text().splitlines():
        value = line.strip('"')
        if value.startswith('-r:'):
            path = local(value[3:]); assert path.is_file(), path
            references[path.relative_to(root).as_posix()] = sha(path)
        elif value.startswith('-resource:'):
            path = local(value[len('-resource:'):].split(',', 1)[0])
            assert path.is_file(), path
            resources[path.relative_to(root).as_posix()] = sha(path)
        elif not value.startswith('-') and value.endswith(('.cs', '.vb')):
            path = local(value); assert path.is_file(), path
            if path.is_relative_to(source):
                relative = path.relative_to(source).as_posix()
                assert sha(path) == inputs['original_file_sha256'][relative], relative
                original[relative] = sha(path)
            else:
                relative = path.relative_to(root).as_posix()
                assert relative.startswith(('m/build/refpack-8.0.31/', 'm/build/libraries-8.0.31/')), relative
                generated[relative] = sha(path)
    return dict(original_inputs=original, generated_inputs=generated,
                references=references, resources=resources)

# Reconstruct every build-helper overlay from the pinned archive. In particular,
# the plan-kind correction must not hide an unrecorded project-selection change.
adapters = json.loads((root / 'framework-build-adapters.json').read_text())
version_adapters = {item['path']: item for item in json.loads((root / 'file-version-adapters.json').read_text())} if (root / 'file-version-adapters.json').is_file() else {}
archive = root / 'm/src/dotnet-bootstrap-source.tar.gz'
if not archive.is_file():
    archive = root.parent / 'bootstrap.tar.gz'  # Earlier isolated build roots.
assert sha(archive) == '75bea2026b90c7f4d5f88279c4ff25ca07b4a6eb0e435df90a41c6f485f773c6'
with tarfile.open(archive) as tar:
    for item in adapters:
        relative = 'dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f/bootstrap/m612/' + item['path']
        original = tar.extractfile(relative).read()
        if item['path'] in version_adapters:
            adjusted = original.decode()
            for pair in version_adapters[item['path']]['replacements']:
                assert adjusted.count(pair['before']) == pair['count'], relative
                adjusted = adjusted.replace(pair['before'], pair['after'])
            original = adjusted.encode()
        assert hashlib.sha256(original).hexdigest() == item['original_sha256'], relative
        prepared = original.decode()
        for pair in item['replacements']:
            assert prepared.count(pair['before']) == 1, relative
            prepared = prepared.replace(pair['before'], pair['after'])
        actual = root / 'bootstrap' / relative
        assert actual.read_text() == prepared, relative
        assert sha(actual) == item['prepared_sha256'], relative

props = ET.parse(source / 'src/libraries/NetCoreAppLibrary.props').getroot()
netfx = props.findtext('.//NetFxReference')
required = set(filter(None, (x.strip() for x in props.findtext('.//NetCoreAppLibrary').replace('$(NetFxReference)', netfx).split(';'))))
ref_build = root / 'm/build/refpack-8.0.31'
ref_plan = [line.split('\t') for line in (ref_build / 'plan.tsv').read_text().splitlines()]
expected_refs = {name for name in required if not name.startswith('System.Private.')}
assert {row[0] for row in ref_plan if row[1] != 'stub'} == expected_refs
assert {p.stem for p in (ref_build / 'out').glob('*.dll')} == expected_refs
report = dict(runtime_version='8.0.31', runtime_source_commit=inputs['runtime_source_commit'],
              required_framework_assemblies=sorted(required), references={}, libraries={},
              full_serviced_runtime_completed=False)
for name, kind, project, dependencies in ref_plan:
    label = 'stub-' + name.removeprefix('stub:') if kind == 'stub' else name
    path = ref_build / ('stubs' if kind == 'stub' else 'out') / (name.removeprefix('stub:') + '.dll')
    assert path.is_file(), path
    data = compiler_inputs(ref_build / 'obj' / (label + '.rsp'))
    assert data['original_inputs'] or kind == 'stub', name
    log = (ref_build / 'obj' / (label + '.log')).read_text()
    assert not re.search(r'\berror (?:CS|BC)\d+:', log), name
    data.update(kind=kind, sha256=sha(path), size=path.stat().st_size)
    report['references'][name] = data

if not args.references_only:
    build = root / 'm/build/libraries-8.0.31'
    output = root / 'm/netcoreapp-8.0.31-coreclr/lib'
    assert not (build / 'failed.txt').read_text().strip()
    plan = [line.split('\t') for line in (build / 'plan.tsv').read_text().splitlines()]
    assert {row[0] for row in plan} | {'System.Private.CoreLib', 'Microsoft.VisualBasic.Core'} == required
    assert {p.stem for p in output.glob('*.dll')} == required
    assert (build / 'skipped.tsv').read_text().strip() == 'Microsoft.VisualBasic.Core\tVB project: built by libraries.sh step vb'
    for name, kind, tfm, dependencies in plan:
        folder = build / 'p' / name
        properties = dict(line.split('=', 1) for line in (folder / 'props').read_text().splitlines())
        assert kind == properties['kind'] in {'src', 'shim'}, (name, kind, properties['kind'])
        payload = output / (name + '.dll')
        if kind == 'shim' and name not in {'System', 'System.Data', 'mscorlib'}:
            assert payload.read_bytes() == (ref_build / 'out' / payload.name).read_bytes(), name
            data = dict(origin='new-source-reference-shim', sha256=sha(payload))
        else:
            data = compiler_inputs(build / 'obj' / (name + '.rsp'))
            if properties['GeneratePlatformNotSupportedAssembly'] == 'true' or properties['GeneratePlatformNotSupportedAssemblyMessage']:
                # Windows' JS interop facade is deliberately PNSE upstream. Verify
                # every generated source against its full original reference file.
                originals = {}
                for original in sorted((source / 'src/libraries' / name / 'ref').glob('*.cs')):
                    relative = original.relative_to(source).as_posix()
                    assert sha(original) == inputs['original_file_sha256'][relative]
                    derived = build / 'obj' / name / 'src' / original.name
                    assert derived.read_text() == original.read_text().replace('throw null;', 'throw new System.PlatformNotSupportedException();'), original
                    originals[relative] = sha(original)
                assert originals and not data['original_inputs'], name
                data['upstream_pnse_reference_sources'] = originals
            else:
                expected = {local(line).relative_to(source).as_posix() for line in (folder / 'items').read_text().splitlines()}
                assert expected <= set(data['original_inputs']), (name, sorted(expected - set(data['original_inputs'])))
            assert payload.read_bytes() == (build / 'obj' / name / payload.name).read_bytes(), name
            data.update(origin='new-source-implementation', sha256=sha(payload))
        report['libraries'][name] = data
    name = 'Microsoft.VisualBasic.Core'
    payload = output / (name + '.dll')
    data = compiler_inputs(build / 'obj' / name / 'vbc.rsp')
    assert data['original_inputs'] and any(p.endswith('.vb') for p in data['original_inputs']), name
    assert payload.read_bytes() == (build / 'obj' / name / payload.name).read_bytes()
    data.update(origin='new-source-vb-implementation', sha256=sha(payload))
    report['libraries'][name] = data

destination = root / ('reference-audit.json' if args.references_only else 'framework-audit.json')
destination.write_text(json.dumps(report, indent=2) + '\n')
print('PASS complete original framework manifest, independently reconstructed helper overlays,',
      len(report['references']), 'reference/stub outputs and', len(report['libraries']),
      'library compiler-input/output hashes; runtime integration and packaging remain required')
