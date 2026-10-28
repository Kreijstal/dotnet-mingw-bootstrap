#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build.sh PREPARED_ROOT}" && pwd)
out=$(cd "${2:-$root/illink10-1}" && pwd)
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_ROOT_X64="$DOTNET_ROOT"
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home")
export NUGET_PACKAGES=$(cygpath -am "$out/cache")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/illink/Build.csproj")" \
  -c Release --configfile "$(cygpath -am "$out/NuGet.config")" \
  -p:UseSharedCompilation=false -v:minimal > "$out/build.log" 2>&1
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/NormalizeBootstrapDeps/Build.csproj")" \
  -c Release --configfile "$(cygpath -am "$out/NuGet.config")" \
  -p:UseSharedCompilation=false -v:minimal > "$out/normalize-build.log" 2>&1
/clang64/lib/dotnet/dotnet.exe \
  "$(cygpath -am "$out/NormalizeBootstrapDeps/bin/Release/net8.0/NormalizeBootstrapDeps.dll")" \
  "$(cygpath -am "$out/illink/bin/Release/net8.0/illink.deps.json")" > "$out/normalize.log" 2>&1
/clang64/lib/dotnet/dotnet.exe "$(cygpath -am "$out/illink/bin/Release/net8.0/illink.dll")" \
  --help > "$out/help.log" 2>&1
printf '%s\n' 'Original source10 ILLink and pinned Cecil compiled and CLI loaded; original library rewrite remains required'
