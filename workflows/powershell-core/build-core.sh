#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-core.sh BUILD_ROOT}" && pwd);source=$root/PowerShell-7.4.20
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
export POWERSHELL_TELEMETRY_OPTOUT=1
export DOTNET_ROOT_X64=$DOTNET_ROOT DOTNET_CLI_HOME=$(cygpath -am "$root/cli-home") NUGET_PACKAGES=$(cygpath -am "$root/empty-packages")
cd "$root"
project=$(cygpath -am "$source/src/System.Management.Automation/System.Management.Automation.csproj")
/clang64/lib/dotnet/dotnet.exe restore "$project" --configfile "$(cygpath -am "$root/NuGet.config")" -p:Configuration=Release -v:minimal
/clang64/lib/dotnet/dotnet.exe msbuild "$project" -t:GetCoreSourceReferences -p:Configuration=Release -v:minimal
/clang64/lib/dotnet/dotnet.exe "$(cygpath -am "$source/src/TypeCatalogGen/bin/Release/net8.0/TypeCatalogGen.dll")" "$(cygpath -am "$source/src/System.Management.Automation/CoreCLR/CorePsTypeCatalog.cs")" "$(cygpath -am "$root/core-references.inc")"
/clang64/lib/dotnet/dotnet.exe build "$project" -c Release --no-restore -p:UseSharedCompilation=false -v:minimal
