"""Independently map every staged binary to source project or package bytes."""
import os
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import zipfile

root, output = (Path(p).resolve() for p in sys.argv[1:])
source = root / 'PowerShell-7.4.20'
sha = lambda data: hashlib.sha256(data).hexdigest()
stage = json.loads((root / (output.name + '-stage.json')).read_text())
expected = dict(stage['original_console_files'])
expected.update({name: item['sha256'] for name, item in stage['separate_modules'].items()})
expected.update({'ref/' + name: digest for name, digest in stage['references'].items()})
feed = Path(os.environ['MINGW_PREFIX']+'/lib/dotnet/library-packs')
for name, record in stage['gallery_modules'].items():
    archive = feed / record['source_archive']
    assert sha(archive.read_bytes()) == record['sha256'], archive
    with zipfile.ZipFile(archive) as z:
        for relative, digest in record['assets'].items():
            assert sha(z.read(relative)) == digest, (name, relative)
            expected['Modules/' + name + '/' + relative] = digest
actual = {p.relative_to(output).as_posix(): sha(p.read_bytes()) for p in output.rglob('*') if p.is_file()}
assert actual == expected, (sorted(set(expected) - set(actual)), sorted(set(actual) - set(expected)))

projects = {name + '.dll': source / 'src' / name / 'bin/Release/net8.0' / (name + '.dll') for name in [
    'System.Management.Automation', 'Microsoft.PowerShell.CoreCLR.Eventing',
    'Microsoft.PowerShell.Commands.Utility', 'Microsoft.PowerShell.Commands.Management',
    'Microsoft.PowerShell.Security', 'Microsoft.Management.Infrastructure.CimCmdlets',
    'Microsoft.PowerShell.ConsoleHost', 'Microsoft.PowerShell.Commands.Diagnostics',
    'Microsoft.WSMan.Management', 'Microsoft.WSMan.Runtime',
    'Microsoft.PowerShell.LocalAccounts', 'Microsoft.PowerShell.SDK']}
projects['pwsh.dll'] = source / 'src/powershell-win-core/bin/Release/net8.0/pwsh.dll'
if (output / 'Microsoft.PowerShell.GraphicalHost.dll').is_file():
    projects['Microsoft.PowerShell.GraphicalHost.dll'] = source / 'src/Microsoft.Management.UI.Internal/bin/Release/net8.0/Microsoft.PowerShell.GraphicalHost.dll'
assets = {}
for package in sorted(feed.glob('*.nupkg')):
    with zipfile.ZipFile(package) as z:
        for name in z.namelist():
            if name.lower().endswith(('.dll', '.exe')):
                assets.setdefault(sha(z.read(name)), []).append(dict(archive=package.name, asset=name))
records = {}
used = set()
production = set()
reference = Path(os.environ['MINGW_PREFIX']+'/lib/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0')
for path in sorted(output.rglob('*')):
    if not path.is_file() or path.suffix.lower() not in {'.dll', '.exe'}:
        continue
    relative = path.relative_to(output).as_posix()
    digest = sha(path.read_bytes())
    if relative.startswith('ref/'):
        assert path.read_bytes() == (reference / path.name).read_bytes(), relative
        origin = dict(installed_source_reference=path.name)
    elif relative == 'pwsh.exe':
        native = json.loads((root / 'native-console-source.json').read_text())
        assert native['exact_original_sdk_task_replay_matches'] is True
        assert digest == native['native_executable_sha256']
        assert sha(Path(native['apphost_template']).read_bytes()) == native['apphost_template_sha256']
        assert sha(Path(native['original_source_sdk_task']).read_bytes()) == native['original_source_sdk_task_sha256']
        origin = dict(original_project_native_apphost=native)
    elif path.name in projects:
        assert path.read_bytes() == projects[path.name].read_bytes(), relative
        production.add(path.name)
        origin = dict(original_project_output=projects[path.name].relative_to(source).as_posix())
    else:
        assert digest in assets, ('No source-built package binary', relative)
        origin = dict(installed_source_archives=assets[digest])
        used.update(item['archive'] for item in assets[digest])
    records[relative] = dict(sha256=digest, **origin)
assert production == set(projects), sorted(set(projects) - production)
archives = sorted(used | {p['source_archive'] for p in stage['gallery_modules'].values()})
owners = subprocess.check_output(['pacman', '-Qqo', *[str(feed / name) for name in archives]], text=True).splitlines()
assert len(owners) == len(archives)
report = dict(binary_assets=records, original_project_dlls=len(production),
    source_archive_owners={name: dict(native_owner=owner, sha256=sha((feed / name).read_bytes())) for name, owner in zip(archives, owners)},
    all_staged_files_sha256=actual, source_reference_count=len(stage['references']),
    gallery_modules=sorted(stage['gallery_modules']), full_shipping_powershell_complete=False)
(root / (output.name + '-payload-audit.json')).write_text(json.dumps(report, indent=2) + '\n')
print('PASS all', len(actual), 'staged files,', len(records), 'source binary assets,', len(production),
      'original project DLLs,163 reference assemblies and six source gallery modules; source archive/native owners recorded')
