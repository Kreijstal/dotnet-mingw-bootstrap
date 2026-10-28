#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
layout=$(cd "${1:?usage: check-powershell.sh RUNTIME_LAYOUT}" && pwd)
export DOTNET_ROOT=$(cygpath -am "$layout") DOTNET_ROOT_X64=$(cygpath -am "$layout")
export DOTNET_MULTILEVEL_LOOKUP=0 DOTNET_CLI_TELEMETRY_OPTOUT=1 POWERSHELL_TELEMETRY_OPTOUT=1
timeout 180 "$layout/dotnet.exe" exec --fx-version 8.0.31 \
 /clang64/lib/dotnet/tools/powershell-module-test-host/SourcePowerShell.ModuleHost.dll \
 "$(cygpath -am "$here/PowerShellCheck.ps1")"
