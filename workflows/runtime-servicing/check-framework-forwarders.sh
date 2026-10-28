#!/usr/bin/env bash
set -euo pipefail
layout=$(cd "${1:?usage: check-framework-forwarders.sh RUNTIME_LAYOUT NEW_OUTPUT}" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
mkdir "${2:?missing output}";out=$(cd "$2" && pwd)
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_ROOT_X64=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/cache")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
cp "$here/FrameworkForwarderCheck.cs" "$out/Program.cs"
cat > "$out/Check.csproj" <<'XML'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableDefaultItems>false</EnableDefaultItems><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Compile Include="Program.cs"/><PackageReference Include="System.Data.SqlClient" Version="[4.9.1]"/><PackageReference Include="System.IO.Packaging" Version="[8.0.1]"/><PackageReference Include="System.Security.Cryptography.Pkcs" Version="[8.0.1]"/><PackageReference Include="System.Security.Cryptography.ProtectedData" Version="[8.0.0]"/><PackageReference Include="System.ServiceProcess.ServiceController" Version="[8.0.1]"/><PackageReference Include="System.Threading.AccessControl" Version="[8.0.0]"/></ItemGroup></Project>
XML
cat > "$out/NuGet.config" <<XML
<configuration><packageSources><clear/><add key="installed-source" value="$(cygpath -am /clang64/lib/dotnet/library-packs)"/></packageSources></configuration>
XML
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/Check.csproj")" -c Release --configfile "$(cygpath -am "$out/NuGet.config")" -p:UseSharedCompilation=false -v:minimal
export DOTNET_ROOT=$(cygpath -am "$layout") DOTNET_ROOT_X64=$(cygpath -am "$layout") DOTNET_MULTILEVEL_LOOKUP=0
timeout 90 "$layout/dotnet.exe" exec --fx-version 8.0.31 "$(cygpath -am "$out/bin/Release/net8.0/Check.dll")" "$(cygpath -am "$layout/shared/Microsoft.NETCore.App/8.0.31")"
