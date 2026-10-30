"""Create a private SDK layout, including separately owned source dependencies."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys

stage, out = (Path(p).resolve() for p in sys.argv[1:])
installed = Path(os.environ.get('MINGW_PREFIX', '/clang64')) / 'lib/dotnet'
source = stage / installed.parent.parent.name / 'lib/dotnet'
assert not out.exists()
shutil.copytree(source, out)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
dependencies = {}
for path in (installed / 'sdk/8.0.131').rglob('*'):
    if not path.is_file():
        continue
    relative = path.relative_to(installed)
    target = out / relative
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        dependencies[relative.as_posix()] = sha(path)
    elif relative.as_posix() != 'sdk/8.0.131/Microsoft.NETCoreSdk.BundledVersions.props':
        assert target.read_bytes() == path.read_bytes(), relative
for relative in ['packs/Microsoft.NETCore.App.Ref', 'compiler-packs/roslyn-4.9.2', 'compiler-packs/roslyn-vb-4.9.2',
                 'shared/Microsoft.NETCore.App/8.0.31', 'host/fxr/8.0.31']:
    shutil.copytree(installed / relative, out / relative)
    for path in (installed / relative).rglob('*'):
        if path.is_file():
            dependencies[path.relative_to(installed).as_posix()] = sha(path)
shutil.copyfile(installed / 'dotnet.exe', out / 'dotnet.exe')
dependencies['dotnet.exe'] = sha(installed / 'dotnet.exe')
paths = sorted(dependencies)
owners = subprocess.check_output(['pacman', '-Qqo', *[str(installed / p) for p in paths]], text=True).splitlines()
assert len(owners) == len(paths)
(out.parent / (out.name + '-source-dependencies.json')).write_text(json.dumps({
    p: dict(sha256=dependencies[p], native_owner=owner) for p, owner in zip(paths, owners)
}, indent=2) + '\n')
print('PASS private candidate SDK with', len(dependencies), 'byte-verified separately owned source dependencies; candidate SDK metadata retained')
