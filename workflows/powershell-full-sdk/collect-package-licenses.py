"""Collect complete native-owner licenses for every bundled source dependency."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys

root, app, out = (Path(p).resolve() for p in sys.argv[1:])
assert not out.exists()
out.mkdir()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
payload = json.loads((root / (app.name + '-payload-audit.json')).read_text())
feed = Path('/clang64/lib/dotnet/library-packs')
archives = dict(payload['source_archive_owners'])
for path in (root / 'empty-packages').rglob('*.nupkg'):
    installed = feed / path.name
    assert installed.is_file() and sha(installed) == sha(path), path
    if path.name not in archives:
        archives[path.name] = dict(sha256=sha(path))
names = sorted(archives)
owners = subprocess.check_output(['pacman', '-Qqo', *[str(feed / name) for name in names]], text=True).splitlines()
assert len(names) == len(owners)
for name, owner in zip(names, owners):
    assert sha(feed / name) == archives[name]['sha256']
    if 'native_owner' in archives[name]:
        assert archives[name]['native_owner'] == owner
    archives[name]['native_owner'] = owner
runtime_owners = {record['native_owner'] for record in payload['source_archive_owners'].values()} | {
    'mingw-w64-clang-x86_64-dotnet-runtime8-servicing',
    'mingw-w64-clang-x86_64-dotnet-targeting8-servicing',
    'mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap'}
build_owners = {record['native_owner'] for record in archives.values()} | runtime_owners | {
    'mingw-w64-clang-x86_64-dotnet-sdk8-servicing',
    'mingw-w64-clang-x86_64-dotnet-sdk-codestyle',
    'mingw-w64-clang-x86_64-dotnet-sdk-netanalyzers',
    'mingw-w64-clang-x86_64-dotnet-csharp-compiler'}
records = {}
versions = {}
for line in subprocess.check_output(['pacman','-Q',*sorted(build_owners)],text=True).splitlines():
    owner,version=line.split()
    assert owner in build_owners
    versions[owner]=version
assert set(versions)==build_owners
owner_paths={owner:[] for owner in runtime_owners}
for line in subprocess.check_output(['pacman','-Ql',*sorted(runtime_owners)],text=True).splitlines():
    owner,path=line.split(' ',1)
    assert owner in owner_paths
    owner_paths[owner].append(path)
for owner in sorted(build_owners):
    version=versions[owner]
    if owner not in runtime_owners:
        continue
    paths = owner_paths[owner]
    licenses = [Path(p) for p in paths if '/share/licenses/' in p and not p.endswith('/')]
    assert licenses, ('Source owner has no packaged license files', owner)
    entries = {}
    for path in licenses:
        assert path.is_file() and path.stat().st_size > 0, path
        relative = path.relative_to('/clang64/share/licenses').as_posix()
        target = out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            assert target.read_bytes() == path.read_bytes(), relative
        else:
            shutil.copy2(path, target)
        entries[relative] = dict(sha256=sha(path), bytes=path.stat().st_size, installed_source=str(path))
    records[owner] = dict(version=version, license_files=entries)
for name in ['LICENSE.txt', 'ThirdPartyNotices.txt']:
    path = root / 'PowerShell-7.4.20' / name
    target = out / 'powershell-source' / name
    target.parent.mkdir(exist_ok=True)
    shutil.copy2(path, target)
report = dict(source_nuget_archives=archives, bundled_source_native_owners=records,
    exact_build_package_versions=versions,
    runtime_framework_dependencies={owner:versions[owner] for owner in sorted(runtime_owners) if owner.endswith(('dotnet-runtime8-servicing', 'dotnet-windowsdesktop-bootstrap'))},
    copied_license_files={p.relative_to(out).as_posix():sha(p) for p in out.rglob('*') if p.is_file()},
    original_powershell_licenses={name:sha(root/'PowerShell-7.4.20'/name) for name in ['LICENSE.txt','ThirdPartyNotices.txt']},
    full_dependency_goal_complete=False)
(root / (app.name + '-license-closure.json')).write_text(json.dumps(report, indent=2) + '\n')
print('PASS', len(archives), 'source NuGet archive mappings,', len(records), 'bundled/native framework owners and',
      len(report['copied_license_files']), 'complete license/notice files; exact build versions recorded')
