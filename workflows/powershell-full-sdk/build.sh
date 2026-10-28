#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build.sh BUILD_ROOT}" && pwd)
source=$root/PowerShell-7.4.20
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_ROOT_X64=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_CLI_HOME=$(cygpath -am "$root/cli-home") NUGET_PACKAGES=$(cygpath -am "$root/empty-packages")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 POWERSHELL_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
dotnet=/clang64/lib/dotnet/dotnet.exe
config=$(cygpath -am "$root/NuGet.config")
args=(-c Release --configfile "$config" -p:UseSharedCompilation=false -v:minimal)
for tool in ResGen TypeCatalogGen; do
 "$dotnet" build "$(cygpath -am "$source/src/$tool/$tool.csproj")" "${args[@]}" > "$root/$tool.log" 2>&1
done
(
 cd "$source/src/ResGen"
 "$dotnet" "$(cygpath -am "$source/src/ResGen/bin/Release/net8.0/resgen.dll")"
) > "$root/resource-generation.log" 2>&1
sdk=$(cygpath -am "$source/src/Microsoft.PowerShell.SDK/Microsoft.PowerShell.SDK.csproj")
"$dotnet" restore "$sdk" --configfile "$config" -p:Configuration=Release -v:minimal > "$root/sdk-restore.log" 2>&1
"$dotnet" msbuild "$sdk" -t:_GetDependencies -p:Configuration=Release -p:DesignTimeBuild=true \
 "-p:_DependencyFile=$(cygpath -am "$root/sdk-references.inc")" -v:minimal > "$root/sdk-reference-generation.log" 2>&1
"$dotnet" "$(cygpath -am "$source/src/TypeCatalogGen/bin/Release/net8.0/TypeCatalogGen.dll")" \
 "$(cygpath -am "$source/src/System.Management.Automation/CoreCLR/CorePsTypeCatalog.cs")" "$(cygpath -am "$root/sdk-references.inc")" > "$root/sdk-catalog-generation.log" 2>&1
"$dotnet" build "$sdk" -c Release --no-restore -p:UseSharedCompilation=false -v:minimal > "$root/sdk-build.log" 2>&1
"$dotnet" build "$(cygpath -am "$source/src/powershell-win-core/powershell-win-core.csproj")" \
 "${args[@]}" -p:SDKToUse=Microsoft.NET.Sdk -p:UseAppHost=true > "$root/console-build.log" 2>&1
"$dotnet" build "$(cygpath -am "$source/src/Microsoft.PowerShell.LocalAccounts/Microsoft.PowerShell.LocalAccounts.csproj")" \
 "${args[@]}" > "$root/localaccounts-build.log" 2>&1
echo 'PASS full original SDK/catalog, original Windows console project and original LocalAccounts source builds; source/runtime/shipping checks remain required'
