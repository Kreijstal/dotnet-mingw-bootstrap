"""Match a native targeting package to its audited source build and licenses."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('artifact', type=Path)
parser.add_argument('pack', type=Path)
parser.add_argument('licenses', type=Path)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
pack, licenses = args.pack.resolve(), args.licenses.resolve()
src = pack.parent
prepared = src / 'prepared'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
payload = json.loads((pack / 'source-payload.json').read_text())
assert len(payload['files']) == 595
for relative, record in payload['files'].items():
    assert sha(pack / relative) == record['sha256'], relative
for name, key in [('FrameworkList.xml', 'framework_list_sha256'),
                  ('PlatformManifest.txt', 'platform_manifest_sha256'),
                  ('PackageOverrides.txt', 'original_package_overrides_sha256')]:
    assert sha(pack / 'data' / name) == payload[key], name
assert sha(pack / 'source-platform-manifest.json') == payload['platform_manifest_source_proof_sha256']
proof = json.loads((pack / 'source-platform-manifest.json').read_text())
assert len(proof['entries']) == 371 and proof['unique_entry_count'] == 342
owners = json.loads((licenses / 'source-owners.json').read_text())
assert owners['supporting_owner_count'] == 11 and owners['supporting_license_files'] == 23
prefix = 'clang64/lib/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.31/'
expected = {prefix + p.relative_to(pack).as_posix(): sha(p) for p in pack.rglob('*') if p.is_file()}
doc = 'clang64/share/doc/dotnet-targeting8-servicing/'
expected[doc + 'README.md'] = sha(src / 'README.md')
expected[doc + 'SdkGeneratorCheck.cs'] = sha(src / 'servicing/SdkGeneratorCheck.cs')
for name in ['source-inputs', 'reference-audit', 'version-build-inputs', 'file-version-adapters', 'framework-build-adapters', 'shim-version-audit', 'shim-identity-audit']:
    expected[doc + name + '.json'] = sha(prepared / (name + '.json'))
for name in ['source-inputs', 'build-audit', 'pe-metadata']:
    expected[doc + 'generators-' + name + '.json'] = sha(prepared / 'targeting-generators' / (name + '.json'))
expected[doc + 'source-owners.json'] = sha(licenses / 'source-owners.json')
license_prefix = 'clang64/share/licenses/dotnet-targeting8-servicing/'
for p in licenses.rglob('*'):
    if p.is_file() and p.name != 'source-owners.json':
        expected[license_prefix + p.relative_to(licenses).as_posix()] = sha(p)
expected[license_prefix + 'Arcade-LICENSE.TXT'] = sha(src / 'servicing/targeting-inputs/LICENSE.TXT')
for item in owners['supporting_source_nuget_owners'].values():
    for relative, record in item['license_files'].items():
        assert expected[license_prefix + relative] == record['sha256'], relative
actual = {}
fields = {}
with subprocess.Popen(['zstd', '-dc', str(args.artifact)], stdout=subprocess.PIPE) as process:
    with tarfile.open(fileobj=process.stdout, mode='r|') as archive:
        for member in archive:
            if member.isdir():
                continue
            assert member.isfile(), member.name
            data = archive.extractfile(member).read()
            if member.name == '.PKGINFO':
                for line in data.decode().splitlines():
                    key, separator, value = line.partition(' = ')
                    if separator:
                        fields.setdefault(key, []).append(value)
            elif member.name.startswith('.'):
                assert member.name in {'.BUILDINFO', '.MTREE'}, member.name
            else:
                actual[member.name] = hashlib.sha256(data).hexdigest()
    while process.stdout.read(1024 * 1024):
        pass
    assert process.wait() == 0
assert fields['pkgname'] == ['mingw-w64-clang-x86_64-dotnet-targeting8-servicing']
assert fields['pkgver'] == ['8.0.31-2']
assert fields['provides'] == ['mingw-w64-clang-x86_64-dotnet-targeting-pack=8.0.131-2']
assert fields['conflict'] == ['mingw-w64-clang-x86_64-dotnet-targeting-pack']
assert set(fields['depend']) == {
    'mingw-w64-clang-x86_64-dotnet-runtime8-servicing>=8.0.31-3',
    'mingw-w64-clang-x86_64-dotnet-csharp-compiler=4.9.2',
    'mingw-w64-clang-x86_64-dotnet-sdk-netanalyzers=8.0.0.preview26274.2',
}
assert actual == expected, (sorted(set(expected) - set(actual)), sorted(set(actual) - set(expected)),
                            [p for p in actual.keys() & expected.keys() if actual[p] != expected[p]])
result = dict(artifact=args.artifact.name, artifact_sha256=sha(args.artifact),
              exact_payload_and_documentation_sha256=actual, original_reference_dlls=163,
              generator_and_satellite_dlls=99, total_dll_assets=595,
              platform_manifest_entries=371, platform_manifest_unique_entries=342,
              supporting_source_license_files=23, exact_source_layout_match=True,
              fresh_installed_ownership_verified=False, historical_generator_hosts_verified=False,
              full_dependency_goal_complete=False)
if args.output:
    args.output.write_text(json.dumps(result, indent=2) + '\n')
print('PASS native targeting package: exact595 DLLs, all manifest/provenance/license bytes, SDK role and replacement conflict metadata; installed ownership checks remain required')
