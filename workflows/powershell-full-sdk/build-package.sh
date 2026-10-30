#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "${1:?usage: build-package.sh ROOT [PACKAGE_DIR]}" && pwd)
# PACKAGE_DIR holds the recipe's launcher.c, LICENSE.launcher, README.md,
# check-package.sh, check-launcher.py and module-host-audit-source.py.
# POWERSHELL_DESKTOP=0 leaves out GraphicalHost and the WindowsDesktop console
# (for an environment without the WindowsDesktop runtime).
export POWERSHELL_PACKAGE_DIR=$(cd "${2:-$here/../../mingw-w64-powershell}" && pwd)
export POWERSHELL_DESKTOP=${POWERSHELL_DESKTOP:-1}
package=$POWERSHELL_PACKAGE_DIR
bash "$here/build.sh" "$root"
[ "$POWERSHELL_DESKTOP" = 0 ] || bash "$here/build-desktop.sh" "$root"
/usr/bin/python3 "$here/audit-source.py" "$root"
/usr/bin/python3 "$here/audit-catalog.py" "$root"
[ "$POWERSHELL_DESKTOP" = 0 ] || /usr/bin/python3 "$here/audit-graphical-source.py" "$root" "$root/graphical-source-check"
/usr/bin/python3 "$here/audit-native-console.py" "$root" ${MINGW_PREFIX}/lib/dotnet \
  ${MINGW_PREFIX}/share/doc/dotnet-sdk8-servicing "$root/native-apphost-check"
/usr/bin/python3 "$here/stage.py" "$root" "$root/app"
/usr/bin/python3 "$here/audit-payload.py" "$root" "$root/app"
/usr/bin/python3 "$here/collect-package-licenses.py" "$root" "$root/app" "$root/licenses"
printf '#define TARGET_RELATIVE L"../lib/powershell/7/pwsh.exe"\n' > "$root/launcher-config.h"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -municode \
  -include "$root/launcher-config.h" "$package/launcher.c" -o "$root/pwsh-launcher.exe"
/usr/bin/python3 "$here/stage-package.py" "$root" "$root/app" "$root/licenses" \
  "$root/pwsh-launcher.exe" "$package/LICENSE.launcher" "$root/distribution"
