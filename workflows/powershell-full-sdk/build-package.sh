#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "${1:?usage: build-package.sh ROOT}" && pwd)
package=$(cd "$here/../../mingw-w64-powershell" && pwd)
bash "$here/build.sh" "$root"
bash "$here/build-desktop.sh" "$root"
/usr/bin/python3 "$here/audit-source.py" "$root"
/usr/bin/python3 "$here/audit-catalog.py" "$root"
/usr/bin/python3 "$here/audit-graphical-source.py" "$root" "$root/graphical-source-check"
/usr/bin/python3 "$here/audit-native-console.py" "$root" /clang64/lib/dotnet \
  /clang64/share/doc/dotnet-sdk8-servicing "$root/native-apphost-check"
/usr/bin/python3 "$here/stage.py" "$root" "$root/app"
/usr/bin/python3 "$here/audit-payload.py" "$root" "$root/app"
/usr/bin/python3 "$here/collect-package-licenses.py" "$root" "$root/app" "$root/licenses"
/usr/bin/python3 - "$root/app-license-closure.json" "$package/source-dependency-lock.json" <<'PY'
import json,sys
actual,expected=(json.load(open(p)) for p in sys.argv[1:])
for key in expected:
    assert actual[key]==expected[key],('Source dependency lock mismatch',key)
print('PASS exact source archive hashes/owners and pinned build package versions')
PY
printf '#define TARGET_RELATIVE L"../lib/powershell/7/pwsh.exe"\n' > "$root/launcher-config.h"
/clang64/bin/clang -std=c11 -O2 -Wall -Wextra -Werror -municode \
  -include "$root/launcher-config.h" "$package/launcher.c" -o "$root/pwsh-launcher.exe"
/usr/bin/python3 "$here/stage-package.py" "$root" "$root/app" "$root/licenses" \
  "$root/pwsh-launcher.exe" "$package/LICENSE.launcher" "$root/distribution"
