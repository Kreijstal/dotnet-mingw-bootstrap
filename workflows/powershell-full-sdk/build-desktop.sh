#!/usr/bin/env bash
# Include the original conditional GraphicalHost after the full core build.
set -euo pipefail
root=$(cd "${1:?usage: build-desktop.sh BUILD_ROOT}" && pwd)
source=$root/PowerShell-7.4.20
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_ROOT_X64=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_CLI_HOME=$(cygpath -am "$root/cli-home") NUGET_PACKAGES=$(cygpath -am "$root/empty-packages")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false DOTNET_NOLOGO=1
dotnet=/clang64/lib/dotnet/dotnet.exe
config=$(cygpath -am "$root/NuGet.config")
"$dotnet" build "$(cygpath -am "$source/src/Microsoft.Management.UI.Internal/Microsoft.PowerShell.GraphicalHost.csproj")" \
 -c Release --configfile "$config" -p:BuildProjectReferences=false -p:UseSharedCompilation=false -v:minimal > "$root/graphicalhost-build.log" 2>&1
# The source desktop SDK supplies its framework-reference registrations through
# the original ImportWindowsDesktopTargets feature switch. The console's
# SDKToUse property separately includes its original conditional project edge.
"$dotnet" build "$(cygpath -am "$source/src/powershell-win-core/powershell-win-core.csproj")" \
 -c Release --configfile "$config" -p:BuildProjectReferences=false -p:UseSharedCompilation=false \
 -p:SDKToUse=Microsoft.NET.Sdk.WindowsDesktop -p:ImportWindowsDesktopTargets=true -p:UseAppHost=true -v:minimal > "$root/desktop-console-build.log" 2>&1
echo 'PASS original GraphicalHost and conditional Windows desktop console builds; original GraphicalHost analyzer setting retained; desktop/shipping compatibility remains open'
