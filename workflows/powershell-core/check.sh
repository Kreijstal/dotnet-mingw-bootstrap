#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "${1:?usage: check.sh BUILD_ROOT NEW_OUTPUT}" && pwd)
mkdir "${2:?missing new output}";out=$(cd "$2" && pwd)
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
export POWERSHELL_TELEMETRY_OPTOUT=1
export DOTNET_ROOT_X64=$DOTNET_ROOT DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/empty-packages")
cp "$here/Check.cs" "$out/Program.cs";cp "$root/NuGet.config" "$out/"
cat > "$out/Check.csproj" <<PROJECT
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><EnableDefaultItems>false</EnableDefaultItems><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Compile Include="Program.cs"/><ProjectReference Include="$(cygpath -am "$root/PowerShell-7.4.20/src/System.Management.Automation/System.Management.Automation.csproj")"/></ItemGroup></Project>
PROJECT
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/Check.csproj")" -c Release --configfile "$(cygpath -am "$out/NuGet.config")" -p:UseSharedCompilation=false -v:minimal
timeout 120 /clang64/lib/dotnet/dotnet.exe "$(cygpath -am "$out/bin/Release/net8.0/Check.dll")"
