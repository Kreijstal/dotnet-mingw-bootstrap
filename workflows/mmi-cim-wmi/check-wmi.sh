#!/usr/bin/env bash
# Installed, source-built WMI backend; the Framework-dependent baseline is explicit.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
if [[ ${1:-} == --framework-baseline ]]; then
  shift
  exec bash "$here/check-wmi-framework-baseline.sh" "$@"
fi
out=${1:?usage: check-wmi.sh NEW_OUTPUT_DIRECTORY [--framework-baseline uses historical build root]}
exec bash "$here/../../mingw-w64-dotnet-management/check-installed.sh" "$out" \
  /clang64/share/doc/dotnet-management/localization.json
