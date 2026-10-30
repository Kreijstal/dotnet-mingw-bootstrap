"""Stage the original console output, source module packages and reference pack."""
import os
from pathlib import Path, PurePosixPath
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile

root, output = (Path(p).resolve() for p in sys.argv[1:])
source = root / 'PowerShell-7.4.20'
console = source / 'src/powershell-win-core/bin/Release/net8.0'
assert console.joinpath('pwsh.dll').is_file()
assert not output.exists(), output
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
shutil.copytree(console, output)
record = dict(original_console_files={p.relative_to(console).as_posix(): sha(p)
              for p in console.rglob('*') if p.is_file()}, separate_modules={}, references={}, gallery_modules={})

local = source / 'src/Microsoft.PowerShell.LocalAccounts/bin/Release/net8.0/Microsoft.PowerShell.LocalAccounts.dll'
destination = output / 'Modules/Microsoft.PowerShell.LocalAccounts' / local.name
destination.parent.mkdir(parents=True)
shutil.copyfile(local, destination)
record['separate_modules'][destination.relative_to(output).as_posix()] = dict(
    original_project_output=local.relative_to(source).as_posix(), sha256=sha(local))

references = Path(os.environ['MINGW_PREFIX']+'/lib/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0')
(output / 'ref').mkdir()
for path in sorted(references.glob('*.dll')):
    shutil.copyfile(path, output / 'ref' / path.name)
    record['references'][path.name] = sha(path)
assert len(record['references']) == 163

modules = [('PowerShellGet', 'powershellget.2.2.5'),
           ('PackageManagement', 'packagemanagement.1.4.8.1'),
           ('Microsoft.PowerShell.PSResourceGet', 'microsoft.powershell.psresourceget.1.1.1'),
           ('Microsoft.PowerShell.Archive', 'microsoft.powershell.archive.1.2.6'),
           ('PSReadLine', 'psreadline.2.3.6'), ('ThreadJob', 'threadjob.2.0.3')]
for name, package in modules:
    archive = Path(os.environ['MINGW_PREFIX']+'/lib/dotnet/library-packs') / (package + '.nupkg')
    module = output / 'Modules' / name
    module.mkdir()
    assets = {}
    with zipfile.ZipFile(archive) as z:
        for item in z.infolist():
            path = PurePosixPath(item.filename)
            assert not path.is_absolute() and '..' not in path.parts, path
            if item.is_dir() or item.filename.startswith(('_rels/', 'package/')) or item.filename.endswith('.nuspec') or item.filename in ['[Content_Types].xml', '.signature.p7s']:
                continue
            target = module.joinpath(*path.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            data = z.read(item)
            target.write_bytes(data)
            assets[item.filename] = hashlib.sha256(data).hexdigest()
    assert (module / (name + '.psd1')).is_file(), name
    record['gallery_modules'][name] = dict(source_archive=archive.name, sha256=sha(archive),
        native_owner=subprocess.check_output(['pacman', '-Qqo', str(archive)], text=True).strip(), assets=assets)

(root / (output.name + '-stage.json')).write_text(json.dumps(record, indent=2) + '\n')
print('PASS original console output, LocalAccounts,163 source references and all six source-built gallery modules staged:', output)
