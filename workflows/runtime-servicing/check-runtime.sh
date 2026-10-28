#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "${1:?usage: check-runtime.sh PREPARED_ROOT RUNTIME_LAYOUT NEW_OUTPUT}" && pwd)
layout=$(cd "${2:?missing runtime layout}" && pwd)
bash "$here/build-runtime-check.sh" "$root" "${3:?missing new output}"
out=$(cd "$3" && pwd)
export PATH=/clang64/bin:/usr/bin:$PATH
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_MULTILEVEL_LOOKUP=0
export DOTNET_ROOT=$(cygpath -am "$layout") DOTNET_ROOT_X64=$(cygpath -am "$layout")
"$layout/dotnet.exe" --info
timeout 180 "$layout/dotnet.exe" "$(cygpath -am "$out/RuntimeCheck.dll")" "$(cygpath -am "$layout/shared/Microsoft.NETCore.App/8.0.31")" "$(cygpath -am "$layout/required-framework.txt")"
