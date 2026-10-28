#!/usr/bin/env bash
set -euo pipefail
out=$(realpath -m "$1")
test ! -e "$out"
mkdir -p "$out"
package=mingw-w64-clang-x86_64-dotnet-host10-bootstrap
pacman -Q "$package" > "$out/package-version.txt"
test "$(cat "$out/package-version.txt")" = "$package 10.0.5-1"
pacman -Qql "$package" > "$out/owned-paths.txt"
/usr/bin/python3 - "$out" <<'PY'
from pathlib import Path
import hashlib,json,sys
out=Path(sys.argv[1]);prefix=Path('/clang64')
doc=prefix/'share/doc/dotnet-host10-bootstrap'
manifest=json.loads((doc/'source-payload.json').read_text())
expected={str(prefix/name) for name in manifest['files_sha256']}
expected.add(str(doc/'source-payload.json'))
owned={name for name in (out/'owned-paths.txt').read_text().splitlines() if Path(name).is_file()}
assert owned==expected,(owned-expected,expected-owned)
for name,digest in manifest['files_sha256'].items():
    path=prefix/name
    assert path.resolve().is_relative_to(prefix),name
    assert hashlib.sha256(path.read_bytes()).hexdigest()==digest,name
assert manifest['full_runtime10_built'] is False
assert manifest['full_sdk10_built'] is False
(out/'installed-payload.json').write_text(json.dumps(dict(package='mingw-w64-clang-x86_64-dotnet-host10-bootstrap 10.0.5-1',owned_files=len(owned),all_installed_hashes_match=True,full_runtime10_built=False,full_sdk10_built=False),indent=2)+'\n')
print('PASS installed native host package ownership and all payload hashes')
PY
/clang64/bin/python.exe \
  "$(cygpath -am /clang64/share/doc/dotnet-host10-bootstrap/check-native-host.py)" \
  "$(cygpath -am /clang64/lib/dotnet10-bootstrap)" "$(cygpath -am "$out/native-check")"
