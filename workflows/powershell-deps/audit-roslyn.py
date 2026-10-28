"""Audit the source-built Roslyn 4.9.2 compiler library package artifacts."""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

root = Path('mingw-w64-dotnet-roslyn')
fields = {}
for line in (root / '.SRCINFO').read_text().splitlines():
    key, sep, value = line.strip().partition(' = ')
    if sep:
        fields.setdefault(key, []).append(value)
assert len(fields['source']) == len(fields['sha256sums'])
for source, wanted in zip(fields['source'], fields['sha256sums']):
    assert wanted != 'SKIP'
    if '::' not in source:
        assert hashlib.sha256((root / source).read_bytes()).hexdigest() == wanted, source
cultures = ['cs', 'de', 'es', 'fr', 'it', 'ja', 'ko', 'pl', 'pt-BR', 'ru', 'tr', 'zh-Hans', 'zh-Hant']
artifacts = sorted(root.glob('*.pkg.tar.zst'))
assert len(artifacts) == 2
records = []
for suffix, package_id, assembly, api_count in [
    ('common', 'Microsoft.CodeAnalysis.Common', 'Microsoft.CodeAnalysis', 4189),
    ('csharp', 'Microsoft.CodeAnalysis.CSharp', 'Microsoft.CodeAnalysis.CSharp', 6110),
]:
    matches = [p for p in artifacts if '-codeanalysis-' + suffix + '-4.9.2-' in p.name]
    assert len(matches) == 1
    package = matches[0]
    entries = subprocess.check_output(['tar', '--zstd', '-tf', str(package)], text=True).splitlines()

    def payload(path):
        return subprocess.check_output(['tar', '--zstd', '-xOf', str(package), path])

    doc = 'clang64/share/doc/dotnet-codeanalysis-' + suffix + '/'
    license_dir = 'clang64/share/licenses/dotnet-codeanalysis-' + suffix + '/'
    for name in ['LICENSE', 'THIRD-PARTY-NOTICES.txt']:
        assert license_dir + name in entries
    for name in ['README.md', 'PublicAPI.txt', 'localization.json', 'removed-binaries.json', 'generated-sources.json']:
        assert doc + name in entries
    removed = json.loads(payload(doc + 'removed-binaries.json'))
    assert removed and all(len(e['sha256']) == 64 for e in removed)
    generated = json.loads(payload(doc + 'generated-sources.json'))
    assert len(generated) == 5
    assert all(e['equivalent_after_bom_line_endings_and_console_blank_line_cleanup'] for e in generated)
    baseline = payload(doc + 'PublicAPI.txt').decode().splitlines()
    removed_apis = {s[9:].lstrip('~') for s in baseline if s.startswith('*REMOVED*')}
    expected_apis = {s.lstrip('~') for s in baseline if s and not s.startswith(('#', '*REMOVED*'))} - removed_apis
    assert len(expected_apis) == api_count
    localization = json.loads(payload(doc + 'localization.json'))
    assert {e['assembly'] for e in localization} == {assembly}
    assert {e['culture'] for e in localization} == set(cultures)
    assert len(localization) == (26 if suffix == 'common' else 13)
    localization_count = sum(len(e['strings']) for e in localization)
    nupkg = 'clang64/lib/dotnet/library-packs/' + package_id.lower() + '.4.9.2.nupkg'
    assert [p for p in entries if p.endswith('.nupkg')] == [nupkg]
    info = payload('.PKGINFO').decode()
    assert 'pkgver = 4.9.2-1\n' in info
    assert 'depend = mingw-w64-clang-x86_64-dotnet-runtime=8.0.131\n' in info
    if suffix == 'csharp':
        assert 'depend = mingw-w64-clang-x86_64-dotnet-codeanalysis-common=4.9.2\n' in info
    with zipfile.ZipFile(io.BytesIO(payload(nupkg))) as archive:
        spec = ET.fromstring(archive.read(next(p for p in archive.namelist() if p.endswith('.nuspec'))))
        metadata = next(e for e in spec if e.tag.endswith('}metadata'))
        values = {e.tag.rsplit('}', 1)[-1]: e.text for e in metadata}
        assert values['id'] == package_id and values['version'] == '4.9.2'
        repository = next(e for e in metadata if e.tag.endswith('}repository'))
        assert repository.get('url') == 'https://github.com/dotnet/roslyn'
        assert repository.get('commit') == '9934fb9e3527e1c0c51314e57d4aab30f97e8f9e'
        dependencies = {e.get('id'): e.get('version') for e in spec.iter() if e.tag.endswith('}dependency')}
        assert dependencies == ({} if suffix == 'common' else {'Microsoft.CodeAnalysis.Common': '[4.9.2]'})
        assemblies = [p for p in archive.namelist() if p.endswith('.dll')]
        expected = ['lib/net8.0/' + assembly + '.dll'] + [
            'lib/net8.0/' + culture + '/' + assembly + '.resources.dll' for culture in cultures]
        assert sorted(assemblies) == sorted(expected), assemblies
        assert 'README.md' in archive.namelist()
    records.append({'recipe': str(root), 'package_suffix': 'dotnet-codeanalysis-' + suffix,
                    'artifact': package.name, 'sha256': hashlib.sha256(package.read_bytes()).hexdigest(),
                    'nuget_id': package_id, 'nuget_version': '4.9.2', 'framework': 'net8.0',
                    'source_commit': repository.get('commit'), 'source_sha256': fields['sha256sums'][0],
                    'dependencies': dependencies, 'removed_binary_count': len(removed),
                    'regenerated_source_count': 5, 'upstream_api_declarations': api_count,
                    'satellite_cultures': cultures, 'localized_resource_sets': len(localization),
                    'localized_resource_strings': localization_count})
    print(package_id + ': checksums, licenses, exact dependency/payload, regeneration, API and localization records PASS')
assert sum(e['localized_resource_strings'] for e in records) == 35750
if len(sys.argv) == 2:
    Path(sys.argv[1]).write_text(json.dumps(records, indent=2) + '\n')
