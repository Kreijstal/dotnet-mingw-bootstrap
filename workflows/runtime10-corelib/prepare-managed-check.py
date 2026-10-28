"""Stage an isolated source10 CoreLib/native layout and corelib-only managed probe."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys

root, out = (Path(p).resolve() for p in sys.argv[1:])
here = Path(__file__).resolve().parent
assert not out.exists(), out
layout = out / 'native CLR10 λ'
layout.mkdir(parents=True)
native_root = subprocess.check_output(['cygpath', '-am', str(root)], text=True).strip()
def native(path):
    path = Path(path).resolve()
    if path.is_relative_to(root):
        return native_root + '/' + path.relative_to(root).as_posix()
    return subprocess.check_output(['cygpath', '-am', str(path)], text=True).strip()
paths = {
    'corerun.exe': root / 'coreclr-build/hosts/corerun/corerun.exe',
    'coreclr.dll': root / 'coreclr-build/dlls/mscoree/coreclr/coreclr.dll',
    'clrjit.dll': root / 'coreclr-build/jit/clrjit.dll',
    'mscorrc.dll': root / 'coreclr-build/dlls/mscorrc/mscorrc.dll',
    'clretwrc.dll': root / 'coreclr-build/dlls/clretwrc/clretwrc.dll',
    'System.Private.CoreLib.dll': root / 'corelib10-1/corelib/lib/System.Private.CoreLib.dll'}
for name, path in paths.items():
    assert path.is_file(), path
    shutil.copy2(path, layout / name)
args = ['/nostdlib+', '/target:exe', '/optimize+', '/nullable:enable', '/langversion:preview',
        '/out:"' + native(layout / 'ManagedProbe.dll') + '"',
        '/reference:"' + native(layout / 'System.Private.CoreLib.dll') + '"',
        '"' + native(here / 'inputs/ManagedProbe.cs') + '"']
(out / 'probe.rsp').write_text('\n'.join(args) + '\n')
record = dict(source_runtime_version='10.0.5',
              original_files_sha256={name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in paths.items()},
              probe_cs_sha256=hashlib.sha256((here / 'inputs/ManagedProbe.cs').read_bytes()).hexdigest(),
              corelib_fcall_rva_rewrite_required=True, full_original_illink_rewrite_completed=False,
              managed_execution_verified=False, full_runtime10_built=False,
              full_dependency_goal_complete=False)
(out / 'layout-inputs.json').write_text(json.dumps(record, indent=2) + '\n')
print('Staged isolated source10 corerun, native runtime and CoreLib; execution not yet verified')
