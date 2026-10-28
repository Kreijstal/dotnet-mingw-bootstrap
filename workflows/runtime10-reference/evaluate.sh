#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: evaluate.sh PREPARED_ROOT OUTPUT}" && pwd)
out=$(cd "$2" && pwd)
sdk=/clang64/lib/dotnet/sdk/8.0.131
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_ROOT_X64=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/cache")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/evaluation-tool/Build.csproj")" \
 -c Release --configfile "$(cygpath -am "$out/NuGet.config")" \
 -p:UseSharedCompilation=false -v:minimal > "$out/evaluation-build.log" 2>&1
export MSBuildSDKsPath=$(cygpath -am "$sdk/Sdks") MSBUILD_EXE_PATH=$(cygpath -am "$sdk/MSBuild.dll")
export MSBuildExtensionsPath=$(cygpath -am "$sdk") DOTNET_HOST_PATH=$(cygpath -am /clang64/lib/dotnet/dotnet.exe)
/clang64/lib/dotnet/dotnet.exe \
 "$(cygpath -am "$out/evaluation-tool/bin/Release/net8.0/EvaluateReferenceProjects.dll")" \
 "$(cygpath -am "$out/wrappers.json")" "$(cygpath -am "$out/evaluated.json")" > "$out/evaluation.log" 2>&1
/clang64/lib/dotnet/dotnet.exe msbuild "$(cygpath -am "$out/GenerateStubs.proj")" \
 -t:Generate -nologo -v:minimal > "$out/generate-stubs.log" 2>&1
