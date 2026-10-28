"""Audit the ten produced dependency packages independently of build templates."""
from pathlib import Path
import hashlib
import json
import sys
import xml.etree.ElementTree as E
import zipfile

root = Path(sys.argv[1]).resolve()
records = {}
runtime = json.loads((root / 'runtime9-source-inputs.json').read_text())
for phase, version, commit, repo in [
    ('runtime9', '9.0.0', '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3', 'https://github.com/dotnet/runtime'),
    ('roslyn414', '4.14.0', '8edf7bcd4f1594c3d68a6a567469f41dbd33dd1b', 'https://github.com/dotnet/roslyn')]:
    expected_ids = set(runtime['projects']) if phase == 'runtime9' else {'Microsoft.CodeAnalysis.Common', 'Microsoft.CodeAnalysis.CSharp'}
    actual_ids = set()
    for package in (root / phase / 'feed').glob('*.nupkg'):
        with zipfile.ZipFile(package) as archive:
            spec = E.fromstring(archive.read(next(p for p in archive.namelist() if p.endswith('.nuspec'))))
            metadata = next(e for e in spec if e.tag.endswith('metadata'))
            values = {e.tag.rsplit('}', 1)[-1]: e.text for e in metadata}
            repository = next(e for e in metadata if e.tag.endswith('repository'))
            ident = values['id']
            assert ident in expected_ids and ident not in actual_ids
            actual_ids.add(ident)
            assert values['version'] == version and repository.get('commit') == commit and repository.get('url') == repo
            dlls = [p for p in archive.namelist() if p.endswith('.dll')]
            assembly = 'Microsoft.CodeAnalysis' if ident == 'Microsoft.CodeAnalysis.Common' else ident
            assert 'lib/net8.0/' + assembly + '.dll' in dlls
            assert all(p.startswith('lib/net8.0/') for p in dlls)
            dependencies = {e.get('id'): e.get('version') for e in metadata.iter() if e.tag.endswith('dependency')}
            if phase == 'runtime9':
                assert dependencies == {d: '[9.0.0]' for d in runtime['projects'][ident]['dependencies']}
            records[ident] = dict(version=version, dependencies=dependencies, managed_dlls=len(dlls),
                                  all_managed_assets_net8=True, sha256=hashlib.sha256(package.read_bytes()).hexdigest())
    assert actual_ids == expected_ids
assert len(records) == 10
(root / 'nuget-artifact-audit.json').write_text(json.dumps(records, indent=2) + '\n')
print('PASS ten source NuGet IDs/versions/repositories/commits, exact dependency metadata and managed asset groups')
