"""Audit complete CoreLib10/compiler selections and generated payloads."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import xml.etree.ElementTree as E

root = Path(sys.argv[1]).resolve()
source = root / 'r'
out = root / 'corelib10-1/corelib'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
selection = json.loads((out / 'source-inputs.json').read_text())
assert selection['production_cs_changes'] == 0
for name, digest in selection['original_cs'].items():
    assert sha(source / name) == digest, name
assert len(selection['original_cs']) == 1932
port = json.loads((root / 'workflow/port-source-inputs.json').read_text())
for name, item in port['changed_files'].items():
    assert sha(source / name) == item['ported_sha256'], name
for name in ['dlls/mscoree/exports.cpp', 'vm/corhost.cpp']:
    text = (source / 'src/coreclr' / name).read_text()
    assert 'TRACE CLR10' not in text and 'TRACE AD ' not in text, name

native_root = subprocess.check_output(['cygpath', '-am', str(root)], text=True).strip()
response = (out / 'corelib.rsp').read_text()
assert '/nostdlib+' in response and '/reference:' not in response
normalized = response.replace('\\', '/')
for name in selection['original_cs']:
    assert '"' + native_root + '/r/' + name + '"' in normalized, name
log = (out / 'compile.log').read_text()
assert 'error CS' not in log and 'warning CS8784' not in log and 'warning CS8785' not in log
generated = out / 'obj/generated'
required = [
    'Microsoft.Interop.LibraryImportGenerator/Microsoft.Interop.LibraryImportGenerator/LibraryImports.g.cs',
    'System.Private.CoreLib.Generators/Generators.ProductVersionInfoGenerator/ProductVersionInfo.g.cs',
    'System.Private.CoreLib.Generators/Generators.NativeRuntimeEventSourceGenerator/NativeRuntimeEventSource.g.cs']
for name in required:
    assert (generated / name).is_file() and (generated / name).stat().st_size > 0, name
assert 'new Version(10, 0, 5)' in (generated / required[1]).read_text()
roots = E.parse(out / 'obj/ILLink.Descriptors.xml')
root_types = [item.get('fullname') for item in roots.iter('type')]
assert {'System.__Canon', 'System.Object'} <= set(root_types)

generator_source = json.loads((root / 'corelib10-1/generators/source-inputs.json').read_text())
generator_records = {}
for name, item in generator_source.items():
    for path, digest in item['original_cs'].items():
        assert sha(source / path) == digest, path
    binary = root / 'corelib10-1/generators' / name / 'bin/Release/net8.0' / (name + '.dll')
    assert binary.is_file(), name
    generator_records[name] = dict(original_cs=len(item['original_cs']), production_cs_changes=0,
                                   dll_sha256=sha(binary), bootstrap_framework='net8.0')
metadata = json.loads((out / 'metadata-raw.json').read_text())
assert metadata['name'] == 'System.Private.CoreLib' and metadata['version'] == '10.0.0.0'
assert metadata['public_key_token'] == '7cec85d7bea7798e'
assert metadata['assembly_references'] == 0 and metadata['resource_strings'] == 1421
assert metadata['all_original_resource_values_match'] is True
record = dict(source_runtime_version='10.0.5', original_corelib_cs=1932,
              all_original_source_hashes_match=True, original_native_port_hashes_match=True,
              diagnostic_native_traces_removed=True, production_cs_changes=0,
              raw_corelib_sha256=sha(out / 'lib/System.Private.CoreLib.dll'),
              generated_sources_sha256={p.relative_to(generated).as_posix(): sha(p) for p in generated.rglob('*.cs')},
              original_generators=generator_records, original_runtime_root_types=len(root_types),
              raw_metadata=metadata, raw_compile_warning_lines=log.count(': warning '),
              full_original_illink_rewrite_completed=False, full_runtime10_built=False,
              full_sdk10_built=False, full_dependency_goal_complete=False)
(out / 'source-audit.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS full1932-source CoreLib10, four original generators, generated version/event/interop code, resources and restored native port')
