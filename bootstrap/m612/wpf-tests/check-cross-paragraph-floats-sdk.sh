#!/bin/bash
set -euo pipefail
sdk=$1
work=$2
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$work"
cp "$here/CrossParagraphFloats.cs" "$work/Program.cs"
cat > "$work/CrossParagraphFloatsSdkCheck.csproj" <<'PROJECT'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0-windows</TargetFramework><OutputType>Exe</OutputType><UseWPF>true</UseWPF><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>
PROJECT
printf '<configuration><packageSources><clear /></packageSources></configuration>\n' > "$work/NuGet.config"
export DOTNET_ROOT=$(cygpath -m "$sdk")
export DOTNET_ROOT_X64=$DOTNET_ROOT
export DOTNET_CLI_HOME=$(cygpath -m "$work/cli-home")
export NUGET_PACKAGES=$(cygpath -m "$work/empty-packages")
"$sdk/dotnet.exe" build "$(cygpath -m "$work/CrossParagraphFloatsSdkCheck.csproj")" -c Release \
  --configfile "$(cygpath -m "$work/NuGet.config")" -v:q -p:UseSharedCompilation=false
# Shared-framework execution must load the installed/overlaid desktop pack.
test ! -e "$work/bin/Release/net8.0-windows/PresentationCore.dll"
# This combines 52 layout cases, including optimal-line caret reconstruction and
# compacting GC. Two optimal cases alone took 65-89 seconds on the installed
# reference build; retain all assertions with a bounded budget for the full set.
timeout 600 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0-windows/CrossParagraphFloatsSdkCheck.dll")"
"$sdk/dotnet.exe" publish "$(cygpath -m "$work/CrossParagraphFloatsSdkCheck.csproj")" -r win-x64 --self-contained true \
  --configfile "$(cygpath -m "$work/NuGet.config")" -p:UseAppHost=true -v:q -p:UseSharedCompilation=false
# Verify the published implementation/resource bytes came from this SDK's pack.
cmp "$work/bin/Release/net8.0-windows/win-x64/publish/PresentationCore.dll" \
  "$sdk/shared/Microsoft.WindowsDesktop.App/8.0.0-bootstrap.wpf.8.0.0/PresentationCore.dll"
cmp "$work/bin/Release/net8.0-windows/win-x64/publish/PresentationNative_cor3.dll" \
  "$sdk/shared/Microsoft.WindowsDesktop.App/8.0.0-bootstrap.wpf.8.0.0/PresentationNative_cor3.dll"
env -u DOTNET_ROOT -u DOTNET_ROOT_X64 PATH=/usr/bin:/c/Windows/System32 \
  timeout 600 "$work/bin/Release/net8.0-windows/win-x64/publish/CrossParagraphFloatsSdkCheck.exe"
printf 'PASS normal offline WPF SDK cross-paragraph floats check and self-contained publish with SDK roots removed\n'
