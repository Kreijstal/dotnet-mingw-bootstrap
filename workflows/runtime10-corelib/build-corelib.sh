#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-corelib.sh PREPARED_ROOT}" && pwd)
out="$root/corelib10-1/corelib"
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_ROOT_X64="$DOTNET_ROOT"
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home")
export NUGET_PACKAGES=$(cygpath -am "$out/cache")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
for name in roots fcall; do
  /clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/tools/$name/Build.csproj")" \
    -c Release --configfile "$(cygpath -am "$out/tools/NuGet.config")" \
    -p:UseSharedCompilation=false -v:minimal > "$out/$name-build.log" 2>&1
done
/clang64/lib/dotnet/dotnet.exe msbuild "$(cygpath -am "$out/ResourcesAndRoots.proj")" \
  -nologo -t:ResourcesAndRoots -v:minimal > "$out/resources-roots.log" 2>&1
MSYS2_ARG_CONV_EXCL=/noconfig /clang64/lib/dotnet/dotnet.exe "$(cygpath -am "$root/cdac-bootstrap5/compiler/CompilerHost.dll")" \
  /noconfig "@$(cygpath -am "$out/corelib.rsp")" > "$out/compile.log" 2>&1
printf '%s\n' 'Original CoreLib10 compiled; IL rewriting and managed runtime execution remain required'
