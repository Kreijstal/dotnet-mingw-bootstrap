#!/usr/bin/env bash
set -euo pipefail
out=$(cd "${1:?usage: build-runtime9.sh PREPARED_OUTPUT}" && pwd)
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_ROOT_X64=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/cache")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
mkdir -p "$out/feed"
for name in System.Collections.Immutable System.Reflection.Metadata System.Composition.AttributedModel System.Composition.Runtime System.Composition.Hosting System.Composition.Convention System.Composition.TypedParts System.IO.Pipelines; do
 /clang64/lib/dotnet/dotnet.exe pack "$(cygpath -am "$out/$name/Build.csproj")" -c Release \
  --configfile "$(cygpath -am "$out/NuGet.config")" -o "$(cygpath -am "$out/feed")" \
  -p:UseSharedCompilation=false -v:minimal > "$out/$name/build.log" 2>&1
 printf 'PASS original source9 package: %s\n' "$name"
done
