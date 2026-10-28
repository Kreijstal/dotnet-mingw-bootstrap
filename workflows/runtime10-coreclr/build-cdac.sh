#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-cdac.sh PREPARED_ROOT}" && pwd)
out="$root/${2:-cdac-bootstrap}"
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_ROOT_X64="$DOTNET_ROOT"
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home")
export NUGET_PACKAGES=$(cygpath -am "$out/cache")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/compiler-host/Build.csproj")" \
  -c Release --configfile "$(cygpath -am "$out/NuGet.config")" \
  -p:UseSharedCompilation=false -v:minimal > "$out/compiler-host-build.log" 2>&1
/clang64/lib/dotnet/dotnet.exe "$(cygpath -am "$out/compiler/CompilerHost.dll")" -version \
  > "$out/compiler-version.log" 2>&1
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/compiler-probe/Build.csproj")" \
  -c Release --configfile "$(cygpath -am "$out/NuGet.config")" \
  -p:UseSharedCompilation=false -v:minimal > "$out/compiler-probe-build.log" 2>&1
/clang64/lib/dotnet/dotnet.exe "$(cygpath -am "$out/compiler-probe/bin/Release/net8.0/CSharp14BootstrapProbe.dll")" \
  > "$out/compiler-probe-run.log" 2>&1
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/tool/Build.csproj")" \
  -c Release --configfile "$(cygpath -am "$out/NuGet.config")" \
  -p:UseSharedCompilation=false -v:minimal > "$out/build.log" 2>&1
/clang64/lib/dotnet/dotnet.exe "$(cygpath -am "$out/tool/bin/Release/net8.0/cdac-build-tool.dll")" \
  compose --help > "$out/compose-help.log" 2>&1
printf '%s\n' 'Original cDAC tool compiled and CLI loaded; source compiler C#14 compile/execution passed; actual native descriptor generation remains required'
