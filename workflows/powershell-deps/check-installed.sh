#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:?usage: check-installed.sh NEW_OUTPUT_DIRECTORY}
mkdir "$out"
out=$(cd "$out" && pwd)
export PATH=/clang64/bin:/usr/bin:$PATH
export DOTNET_ROOT=$(cygpath -m /clang64/lib/dotnet)
export DOTNET_ROOT_X64=$DOTNET_ROOT
export DOTNET_CLI_HOME=$(cygpath -m "$out/cli-home")
export NUGET_PACKAGES=$(cygpath -m "$out/empty-packages")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
cp "$here/check-installed.cs" "$out/Program.cs"
cp "$here/check-installed.csproj" "$out/check-installed.csproj"
cat > "$out/NuGet.config" <<CONFIG
<configuration><packageSources><clear /><add key="installed-source-built" value="$(cygpath -m /clang64/lib/dotnet/library-packs)" /></packageSources></configuration>
CONFIG
cd "$out"
/clang64/lib/dotnet/dotnet.exe build check-installed.csproj -c Release --configfile NuGet.config -p:UseSharedCompilation=false -v:minimal
/clang64/lib/dotnet/dotnet.exe bin/Release/net8.0/check-installed.dll
