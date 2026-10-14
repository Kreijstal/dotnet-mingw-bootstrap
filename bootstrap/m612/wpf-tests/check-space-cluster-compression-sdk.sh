#!/bin/bash
set -euo pipefail
sdk=$1
work=$2
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$work"
cp "$here/SpaceClusterCompression.cs" "$work/Program.cs"
/usr/bin/python3 "$here/make-space-cluster-font.py" "$work/space-cluster.ttf"
cat > "$work/SpaceClusterCompressionSdkCheck.csproj" <<'PROJECT'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0-windows</TargetFramework><OutputType>Exe</OutputType><UseWPF>true</UseWPF><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Content Include="space-cluster.ttf" CopyToOutputDirectory="Always" CopyToPublishDirectory="Always" /></ItemGroup></Project>
PROJECT
printf '<configuration><packageSources><clear /></packageSources></configuration>\n' > "$work/NuGet.config"
export DOTNET_ROOT=$(cygpath -m "$sdk")
export DOTNET_ROOT_X64=$DOTNET_ROOT
export DOTNET_CLI_HOME=$(cygpath -m "$work/cli-home")
export NUGET_PACKAGES=$(cygpath -m "$work/empty-packages")
"$sdk/dotnet.exe" build "$(cygpath -m "$work/SpaceClusterCompressionSdkCheck.csproj")" -c Release \
  --configfile "$(cygpath -m "$work/NuGet.config")" -v:q -p:UseSharedCompilation=false
# Shared-framework execution must load the installed/overlaid desktop pack.
test ! -e "$work/bin/Release/net8.0-windows/PresentationCore.dll"
timeout 60 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0-windows/SpaceClusterCompressionSdkCheck.dll")"
"$sdk/dotnet.exe" publish "$(cygpath -m "$work/SpaceClusterCompressionSdkCheck.csproj")" -r win-x64 --self-contained true \
  --configfile "$(cygpath -m "$work/NuGet.config")" -p:UseAppHost=true -v:q -p:UseSharedCompilation=false
# Verify the published implementation/resource bytes came from this SDK's pack.
cmp "$work/bin/Release/net8.0-windows/win-x64/publish/PresentationCore.dll" \
  "$sdk/shared/Microsoft.WindowsDesktop.App/8.0.0-bootstrap.wpf.8.0.0/PresentationCore.dll"
env -u DOTNET_ROOT -u DOTNET_ROOT_X64 PATH=/usr/bin:/c/Windows/System32 \
  timeout 60 "$work/bin/Release/net8.0-windows/win-x64/publish/SpaceClusterCompressionSdkCheck.exe"
printf 'PASS normal offline WPF SDK source-font space cluster compression check and self-contained publish with SDK roots removed\n'
