#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:?usage: check-wmi.sh OUTPUT_DIRECTORY_WITH_WMI_BUILD_ROOT}
out=$(cd "$out" && pwd)
cp "$here/WmiProbe.cs" "$here/WmiProbe.csproj" "$here/NativeDependencies.cs" "$here/NuGet.config" "$out/"
export PATH=/clang64/bin:/usr/bin:$PATH
export DOTNET_ROOT=$(cygpath -m /clang64/lib/dotnet) DOTNET_CLI_TELEMETRY_OPTOUT=1
export DOTNET_ROOT_X64=$DOTNET_ROOT MSBuildEnableWorkloadResolver=false
export DOTNET_CLI_HOME=$(cygpath -m "$out/cli-home")
export NUGET_PACKAGES=$(cygpath -m "$out/empty-packages")
cd "$out"
/clang64/lib/dotnet/dotnet.exe build WmiProbe.csproj -c Release --configfile NuGet.config \
  -p:BaseIntermediateOutputPath=obj-wmi/ -p:UseSharedCompilation=false -v:minimal > wmi-consumer-build.log 2>&1 || { cat wmi-consumer-build.log; exit 1; }
result=0
timeout 45 /clang64/lib/dotnet/dotnet.exe bin-wmi/net8.0/WmiProbe.dll > wmi-run.log 2>&1 || result=$?
cat wmi-run.log
exit "$result"
