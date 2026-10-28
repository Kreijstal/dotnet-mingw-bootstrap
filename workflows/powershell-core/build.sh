#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "${1:?usage: build.sh BUILD_ROOT}" && pwd)
source=$root/PowerShell-7.4.20
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
export POWERSHELL_TELEMETRY_OPTOUT=1
export DOTNET_ROOT_X64=$DOTNET_ROOT DOTNET_CLI_HOME=$(cygpath -am "$root/cli-home") NUGET_PACKAGES=$(cygpath -am "$root/empty-packages")
cd "$root"
args=(-c Release --configfile "$(cygpath -am "$root/NuGet.config")" -p:UseSharedCompilation=false -v:minimal)
for tool in ResGen TypeCatalogGen;do
 /clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$source/src/$tool/$tool.csproj")" "${args[@]}"
done
cd "$source/src/ResGen"
/clang64/lib/dotnet/dotnet.exe "$(cygpath -am "$source/src/ResGen/bin/Release/net8.0/resgen.dll")"
cd "$root"
bash "$here/build-core.sh" "$root"
