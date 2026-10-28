"""Create a new, audited runtime-only layout; never modify the installed SDK."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
destination = Path(sys.argv[2]).resolve()
audit = json.loads((root / 'framework-audit.json').read_text())
core = json.loads((root / 'build-audit.json').read_text())
assert audit['runtime_source_commit'] == core['runtime_source_commit'] == '1219a42122cf5190c7f512850557e38c422430dc'
assert len(audit['libraries']) + 1 == len(audit['required_framework_assemblies'])
assert not destination.exists(), destination
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
libraries = root / 'm/netcoreapp-8.0.31-coreclr/lib'
copies = {}
file_version = (root / 'file-version.txt').read_text().strip()
for name in audit['required_framework_assemblies']:
    path = libraries / (name + '.dll')
    expected = core['corelib_sha256'] if name == 'System.Private.CoreLib' else audit['libraries'][name]['sha256']
    assert sha(path) == expected, path
    copies[path.name] = path
native = {}
for name in ['coreclr.dll', 'clrjit.dll', 'mscorrc.dll', 'clretwrc.dll']:
    path = root / 'm/coreclr-8.0.31' / name
    assert sha(path) == core['native_output_sha256'][path.relative_to(root).as_posix()]
    native[name] = path
native['hostpolicy.dll'] = root / 'm/corehost-8.0.31/corehost/hostpolicy.dll'
native['System.IO.Compression.Native.dll'] = root / 'm/mono-8.0.31/libs/System.IO.Compression.Native.dll'
host = root / 'm/corehost-8.0.31/corehost'
for name in ['dotnet.exe', 'hostfxr.dll', 'hostpolicy.dll']:
    assert sha(host / name) == core['native_output_sha256'][(host / name).relative_to(root).as_posix()]

framework = destination / 'shared/Microsoft.NETCore.App/8.0.31'
framework.mkdir(parents=True)
(destination / 'host/fxr/8.0.31').mkdir(parents=True)
shutil.copy2(host / 'dotnet.exe', destination / 'dotnet.exe')
shutil.copy2(host / 'hostfxr.dll', destination / 'host/fxr/8.0.31/hostfxr.dll')
runtime_assets = {}
for name, path in sorted(copies.items()):
    metadata = subprocess.check_output(['/ucrt64/bin/monodis', '--assembly', str(path)], text=True, stderr=subprocess.PIPE)
    versions = re.findall(r'^Version:\s*(\S+)', metadata, re.M)
    assert len(versions) == 1, (path, versions)
    runtime_assets[name] = dict(assemblyVersion=versions[0], fileVersion=file_version)
    shutil.copy2(path, framework / name)
for name, path in native.items():
    assert path.is_file(), path
    shutil.copy2(path, framework / name)
target = '.NETCoreApp,Version=v8.0/win-x64'
package = 'Microsoft.NETCore.App.Runtime.win-x64/8.0.31'
deps = dict(runtimeTarget=dict(name=target, signature=''), compilationOptions={},
            targets={'.NETCoreApp,Version=v8.0': {}, target: {package: dict(runtime=runtime_assets, native={n: dict(fileVersion=file_version) for n in native})}},
            libraries={package: dict(type='package', serviceable=True, sha512='')})
(framework / 'Microsoft.NETCore.App.deps.json').write_text(json.dumps(deps, indent=2) + '\n')
(destination / 'required-framework.txt').write_text('\n'.join(audit['required_framework_assemblies']) + '\n')
payload = {p.relative_to(destination).as_posix(): sha(p) for p in destination.rglob('*') if p.is_file()}
(destination / 'source-payload.json').write_text(json.dumps(dict(source_commit=audit['runtime_source_commit'], files=payload, integration_verified=False), indent=2) + '\n')
print('PASS isolated new layout:', len(copies), 'source managed assemblies and', len(native), 'source native framework DLLs;', destination)
