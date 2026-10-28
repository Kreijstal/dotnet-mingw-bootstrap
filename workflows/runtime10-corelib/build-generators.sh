#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-generators.sh PREPARED_ROOT}" && pwd)
out="$root/corelib10-1/generators"
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_ROOT_X64="$DOTNET_ROOT"
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home")
export NUGET_PACKAGES=$(cygpath -am "$out/cache")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
for name in Microsoft.Interop.SourceGeneration Microsoft.Interop.LibraryImportGenerator Microsoft.Interop.ComInterfaceGenerator System.Private.CoreLib.Generators; do
  /clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/$name/Build.csproj")" \
    -c Release --configfile "$(cygpath -am "$out/NuGet.config")" \
    -p:UseSharedCompilation=false -v:minimal > "$out/$name/build.log" 2>&1
  printf 'Compiled original source10 generator: %s\n' "$name"
done
