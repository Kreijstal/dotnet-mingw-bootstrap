#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-targeting-generators.sh PREPARED_ROOT}" && pwd)
out="$root/targeting-generators"
shift
if (( $# == 0 )); then set -- 'JSImport' 'Json3.11' 'Json4.0' 'Json4.4'; fi
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_ROOT_X64=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/cache")
for name in "$@"; do
 /clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/$name/Build.csproj")" -c Release --configfile "$(cygpath -am "$out/NuGet.config")" -p:UseSharedCompilation=false \
  -p:CscToolPath="$(cygpath -am /clang64/lib/dotnet/compiler-packs/roslyn-4.9.2)" -p:CscToolExe=csc49.cmd -v:minimal > "$out/$name.log" 2>&1
done
echo 'PASS requested original targeting generator builds'
