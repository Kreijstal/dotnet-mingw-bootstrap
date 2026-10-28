#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-corerun.sh PREPARED_ROOT}" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
/usr/bin/python3 - "$root" "$here" <<'PY'
from pathlib import Path
import hashlib,json,subprocess,sys
root,here=(Path(p) for p in sys.argv[1:])
record=json.loads((here/'corerun-source-inputs.json').read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(here/'corerun-mingw.patch')==record['patch_sha256']
current={name:sha(root/'r'/name) for name in record['files']}
if any(current[name]!=item['ported_sha256'] for name,item in record['files'].items()):
    assert all(current[name]==item['original_sha256'] for name,item in record['files'].items())
    subprocess.run(['patch','--forward','--fuzz=0','--no-backup-if-mismatch','-p1','-i',str(here/'corerun-mingw.patch')],cwd=root/'r',check=True)
for name,item in record['files'].items():assert sha(root/'r'/name)==item['ported_sha256'],name
(root/'corelib10-1/corerun-port-inputs.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS exact original/ported corerun inputs')
PY
ninja -C "$root/coreclr-build" -j2 corerun > "$root/corelib10-1/corerun-build-final.log" 2>&1
/clang64/bin/llvm-readobj --file-headers --coff-debug-directory \
  "$root/coreclr-build/hosts/corerun/corerun.exe" > "$root/corelib10-1/corerun-pe.txt"
grep -q IMAGE_FILE_MACHINE_AMD64 "$root/corelib10-1/corerun-pe.txt"
grep -q IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT "$root/corelib10-1/corerun-pe.txt"
"$root/coreclr-build/hosts/corerun/corerun.exe" -st > "$root/corelib10-1/corerun-selftest.log" 2>&1
printf '%s\n' 'PASS original corerun build/self-tests, AMD64 and actual CET flag'
