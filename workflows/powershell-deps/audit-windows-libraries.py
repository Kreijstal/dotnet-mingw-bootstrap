"""Audit the five split Windows satellite library package artifacts.

Run from the repository root after a successful clean makepkg build. Native
behavior and upstream API checks belong to the recipe's check() gate.
"""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as E
import zipfile

root = Path('mingw-w64-dotnet-windows-libraries')
fields = {}
for line in (root/'.SRCINFO').read_text().splitlines():
    key, sep, value = line.strip().partition(' = ')
    if sep:
        fields.setdefault(key, []).append(value)
assert len(fields['source']) == len(fields['sha256sums'])
for source, wanted in zip(fields['source'], fields['sha256sums']):
    if '::' not in source:
        assert hashlib.sha256((root/source).read_bytes()).hexdigest() == wanted, source
manifest = json.loads((root/'libraries.json').read_text())
records = []
artifacts = sorted(root.glob('*.pkg.tar.zst'))
assert len(artifacts) == len(manifest), artifacts
for library in manifest:
    suffix, name, version = (library[k] for k in ('suffix', 'id', 'version'))
    matches = [p for p in artifacts if p.name.startswith('mingw-w64-clang-x86_64-dotnet-'+suffix+'-8.0.31-')]
    assert len(matches) == 1, (name, matches)
    package = matches[0]
    entries = subprocess.check_output(['tar', '--zstd', '-tf', str(package)], text=True).splitlines()
    def payload(path):
        return subprocess.check_output(['tar', '--zstd', '-xOf', str(package), path])
    for leaf in ['LICENSE', 'THIRD-PARTY-NOTICES']:
        assert f'clang64/share/licenses/dotnet-{suffix}/{leaf}' in entries
    doc = f'clang64/share/doc/dotnet-{suffix}/'
    assert doc+'README.md' in entries
    removed = json.loads(payload(doc+'removed-binaries.json'))
    assert isinstance(removed, list)
    expected = 'clang64/lib/dotnet/library-packs/'+name.lower()+'.'+version+'.nupkg'
    assert [e for e in entries if e.endswith('.nupkg')] == [expected]
    info = payload('.PKGINFO').decode()
    assert 'pkgver = 8.0.31-1\n' in info
    assert 'depend = mingw-w64-clang-x86_64-dotnet-runtime=8.0.131\n' in info
    with zipfile.ZipFile(io.BytesIO(payload(expected))) as archive:
        spec = E.fromstring(archive.read(next(p for p in archive.namelist() if p.endswith('.nuspec'))))
        metadata = next(e for e in spec if e.tag.endswith('}metadata'))
        values = {e.tag.rsplit('}',1)[-1]:e.text for e in metadata}
        assert values['id'] == name and values['version'] == version, (name, values)
        assert not [e for e in spec.iter() if e.tag.endswith('}dependency')], name
        assemblies = [p for p in archive.namelist() if p.endswith('.dll')]
        tfm = library['framework']
        wanted = ([f'ref/net8.0/{name}.dll', f'runtimes/win/lib/net8.0/{name}.dll']
                  if tfm.endswith('-windows') else [f'lib/net8.0/{name}.dll'])
        assert sorted(assemblies) == sorted(wanted), (name, assemblies)
    record = {'recipe':str(root), 'package_suffix':'dotnet-'+suffix, 'artifact':package.name,
              'sha256':hashlib.sha256(package.read_bytes()).hexdigest(), 'nuget_id':name,
              'nuget_version':version, 'dependencies':[], 'source_tag':'v8.0.31',
              'source_sha256':fields['sha256sums'][0], 'removed_binary_count':len(removed)}
    records.append(record)
    print(name+': recipe checksums, split package metadata, licenses and exact NuGet payload PASS')
if len(sys.argv) == 2:
    Path(sys.argv[1]).write_text(json.dumps(records,indent=2)+'\n')
