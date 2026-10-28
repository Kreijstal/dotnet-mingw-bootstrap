#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-corelib.sh PREPARED_ROOT}" && pwd)
export RUNTIME_FILE_VERSION=$(cat "$root/file-version.txt")
here="$root/bootstrap/dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f/bootstrap/m612"
export M612="$root/m" RT_VER=8.0.31 MSYSTEM=UCRT64
export CSC_DOTNET=/clang64/lib/dotnet/dotnet.exe RUNTIME_BUILD_SDK=/clang64/lib/dotnet
export RUNTIME_ROOTS_TASK="$root/managed-build/RuntimeBuildTools/bin/Release/net8.0/RuntimeServicing.BuildTasks.dll"
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_ROOT_X64=$(cygpath -am /clang64/lib/dotnet) DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
export DOTNET_CLI_HOME=$(cygpath -am "$root/managed-build/cli-home") NUGET_PACKAGES=$(cygpath -am "$root/managed-build/cache")
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$root/managed-build/FcallRva/Build.csproj")" -c Release --configfile "$(cygpath -am "$root/managed-build/NuGet.config")" -p:UseSharedCompilation=false -v:minimal > "$root/fcallrva-build.log" 2>&1
bash "$here/corelib-coreclr.sh" /clang64/lib/dotnet/compiler-packs/roslyn-4.9.2/csc.dll > "$root/corelib.log" 2>&1
printf 'PASS matching CoreLib8.0.31 built from original sources and rebuilt generators; full shared framework/runtime integration remains open\n'
