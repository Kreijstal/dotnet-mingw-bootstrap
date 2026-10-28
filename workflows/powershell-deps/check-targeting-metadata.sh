#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:?usage: check-targeting-metadata.sh NEW_OUTPUT_DIRECTORY}
mkdir "$out"
out=$(cd "$out" && pwd)
cp "$here/check-targeting-metadata.cs" "$out/Program.cs"
cat > "$out/check.csproj" <<'PROJECT'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>
PROJECT
printf '<configuration><packageSources><clear /></packageSources></configuration>\n' > "$out/NuGet.config"
export DOTNET_ROOT=$(cygpath -m /clang64/lib/dotnet)
export DOTNET_ROOT_X64=$DOTNET_ROOT DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
export DOTNET_CLI_HOME=$(cygpath -m "$out/cli-home") NUGET_PACKAGES=$(cygpath -m "$out/empty-packages")
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -m "$out/check.csproj")" -c Release --configfile "$(cygpath -m "$out/NuGet.config")" -p:UseSharedCompilation=false -v:minimal
/clang64/lib/dotnet/dotnet.exe "$(cygpath -m "$out/bin/Release/net8.0/check.dll")" "$DOTNET_ROOT"
