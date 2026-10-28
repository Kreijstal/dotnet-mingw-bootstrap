"""Compare every packaged runtime byte with its audited and tested source layout."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('artifact', type=Path)
parser.add_argument('layout', type=Path)
parser.add_argument('--output', type=Path)
parser.add_argument('--package-version', default='8.0.31-3')
args = parser.parse_args()
layout = args.layout.resolve()
prepared = layout.parent / 'prepared'
sha = lambda data: hashlib.sha256(data).hexdigest()
manifest = json.loads((layout / 'source-payload.json').read_text())
framework = json.loads((prepared / 'framework-audit.json').read_text())
prefix = 'clang64/lib/dotnet/'
doc = 'clang64/share/doc/dotnet-runtime8-servicing/'
expected = {}
for relative, digest in manifest['files'].items():
    if relative.startswith(('shared/Microsoft.NETCore.App/8.0.31/', 'host/fxr/8.0.31/')):
        assert sha((layout / relative).read_bytes()) == digest, relative
        expected[prefix + relative] = digest
assert len(expected) == 176, len(expected)
for name in ['source-inputs', 'build-audit', 'framework-audit', 'framework-build-adapters', 'corelib-build-adapter', 'version-build-inputs', 'file-version-adapters'] + (['shim-version-audit', 'shim-identity-audit'] if args.package_version == '8.0.31-3' else []):
    expected[doc + name + '.json'] = sha((prepared / (name + '.json')).read_bytes())
for name in ['source-payload.json', 'required-framework.txt']:
    expected[doc + name] = sha((layout / name).read_bytes())
expected[doc + 'README.md'] = sha((layout.parent / 'README.md').read_bytes())
for name in ['RuntimeCheck.cs', 'PowerShellCheck.ps1'] + (['FrameworkForwarderCheck.cs', 'check-framework-forwarders.sh'] if args.package_version == '8.0.31-3' else []):
    expected[doc + name] = sha((layout.parent / 'servicing' / name).read_bytes())
assert (layout.parent / 'runtime-check/RuntimeCheck.cs').read_bytes() == (layout.parent / 'servicing/RuntimeCheck.cs').read_bytes()
for name in ['LICENSE.TXT', 'THIRD-PARTY-NOTICES.TXT']:
    expected['clang64/share/licenses/dotnet-runtime8-servicing/' + name] = sha((prepared / 'm/src/runtime-8.0.31-native' / name).read_bytes())
expected['clang64/share/licenses/dotnet-runtime8-servicing/Arcade-LICENSE.TXT'] = sha((layout.parent / 'servicing/inputs/Arcade-LICENSE.TXT').read_bytes())
actual = {}
package_info = None
with subprocess.Popen(['zstd', '-dc', str(args.artifact)], stdout=subprocess.PIPE) as process:
    with tarfile.open(fileobj=process.stdout, mode='r|') as archive:
        for member in archive:
            if member.isdir():
                continue
            assert member.isfile(), member.name
            data = archive.extractfile(member).read()
            if member.name == '.PKGINFO':
                package_info = data.decode()
            elif member.name.startswith('.'):
                assert member.name in {'.BUILDINFO', '.MTREE'}, member.name
            else:
                actual[member.name] = sha(data)
    # Consume tar padding too, so zstd cannot block waiting for its stdout pipe.
    while process.stdout.read(1024 * 1024):
        pass
    assert process.wait() == 0
assert package_info is not None
fields = {}
for line in package_info.splitlines():
    key, separator, value = line.partition(' = ')
    if separator:
        fields.setdefault(key, []).append(value)
assert fields['pkgname'] == ['mingw-w64-clang-x86_64-dotnet-runtime8-servicing']
assert fields['pkgver'] == [args.package_version]
assert fields['depend'] == ['mingw-w64-clang-x86_64-dotnet-runtime=8.0.131']
assert set(actual) == set(expected), (sorted(set(expected) - set(actual)), sorted(set(actual) - set(expected)))
for name, digest in expected.items():
    assert actual[name] == digest, name
assert {Path(p).stem for p in actual if p.startswith(prefix + 'shared/') and Path(p).stem in framework['required_framework_assemblies']} == set(framework['required_framework_assemblies'])
result = dict(artifact=args.artifact.name, artifact_sha256=sha(args.artifact.read_bytes()),
              runtime_version='8.0.31', runtime_source_commit=manifest['source_commit'],
              managed_framework_assemblies=len(framework['required_framework_assemblies']),
              versioned_runtime_payload_files=176, file_sha256=actual,
              source_layout_bytes_match=True, fresh_install_verified=False,
              full_dependency_goal_complete=False)
if args.output:
    args.output.write_text(json.dumps(result, indent=2) + '\n')
print('PASS exact versioned runtime/hostfxr payload, all168 managed assemblies, source-audit/license bytes, dependency metadata and no unlisted files; fresh install/SDK integration remain required')
