#!/usr/bin/env bash
# Validate the installed experimental MMI package with a new cache and no build-tree references.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:?usage: check-installed.sh NEW_OUTPUT_DIRECTORY}
mkdir "$out"
out=$(cd "$out" && pwd)
export PATH=/clang64/bin:/usr/bin:$PATH
export DOTNET_ROOT=$(cygpath -m /clang64/lib/dotnet)
export DOTNET_ROOT_X64=$DOTNET_ROOT DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
export DOTNET_CLI_HOME=$(cygpath -m "$out/cli-home")
export NUGET_PACKAGES=$(cygpath -m "$out/empty-packages")
/clang64/bin/clang -std=c11 -shared -Wall -Wextra -Werror "$here/check-extended-callbacks.c" -o "$out/mmi-callback-driver.dll"
/clang64/bin/clang -std=c11 -shared -Wall -Wextra -Werror "$here/check-streamed-results.c" -o "$out/mmi-stream-driver.dll"
cp "$here/MmiStreamChecks.cs" "$here/MmiExtendedCallbackChecks.cs" "$here/MmiCredentialChecks.cs" "$here/MmiProbe.cs" "$here/MmiPortChecks.cs" "$here/NativeDependencies.cs" "$out/"
cat > "$out/check.csproj" <<'PROJECT'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><PackageReference Include="Microsoft.Management.Infrastructure" Version="[2.0.0-preview.1]" /><None Include="mmi-callback-driver.dll" CopyToOutputDirectory="PreserveNewest" /><None Include="mmi-stream-driver.dll" CopyToOutputDirectory="PreserveNewest" /></ItemGroup></Project>
PROJECT
cat > "$out/NuGet.config" <<CONFIG
<configuration><packageSources><clear /><add key="installed-source-built" value="$(cygpath -m /clang64/lib/dotnet/library-packs)" /></packageSources></configuration>
CONFIG
cd "$out"
/clang64/lib/dotnet/dotnet.exe build check.csproj -c Release --configfile NuGet.config -p:UseSharedCompilation=false -v:minimal
timeout 90 /clang64/lib/dotnet/dotnet.exe bin/Release/net8.0/check.dll
timeout 90 /clang64/lib/dotnet/dotnet.exe bin/Release/net8.0/check.dll --port-checks
timeout 30 /clang64/lib/dotnet/dotnet.exe bin/Release/net8.0/check.dll --capabilities
