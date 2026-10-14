#!/bin/bash
set -euo pipefail
sdk=$1
work=$2
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$work"
cp "$here/Tables.cs" "$work/Program.cs"
cat > "$work/TablesSdkCheck.csproj" <<'PROJECT'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0-windows</TargetFramework><OutputType>Exe</OutputType><UseWPF>true</UseWPF><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>
PROJECT
printf '<configuration><packageSources><clear /></packageSources></configuration>\n' > "$work/NuGet.config"
export DOTNET_ROOT=$(cygpath -m "$sdk")
export DOTNET_ROOT_X64=$DOTNET_ROOT
export DOTNET_CLI_HOME=$(cygpath -m "$work/cli-home")
export NUGET_PACKAGES=$(cygpath -m "$work/empty-packages")
"$sdk/dotnet.exe" build "$(cygpath -m "$work/TablesSdkCheck.csproj")" -c Release \
  --configfile "$(cygpath -m "$work/NuGet.config")" -v:q -p:UseSharedCompilation=false
# Shared-framework execution must load the installed/overlaid desktop pack.
test ! -e "$work/bin/Release/net8.0-windows/PresentationCore.dll"
timeout 60 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0-windows/TablesSdkCheck.dll")"
for directions in 'LeftToRight RightToLeft' 'RightToLeft LeftToRight'; do
  read -r page_direction table_direction <<< "$directions"
  timeout 60 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0-windows/TablesSdkCheck.dll")" "$page_direction" "$table_direction"
done
"$sdk/dotnet.exe" publish "$(cygpath -m "$work/TablesSdkCheck.csproj")" -r win-x64 --self-contained true \
  --configfile "$(cygpath -m "$work/NuGet.config")" -p:UseAppHost=true -v:q -p:UseSharedCompilation=false
# Verify the published implementation/resource bytes came from this SDK's pack.
cmp "$work/bin/Release/net8.0-windows/win-x64/publish/PresentationCore.dll" \
  "$sdk/shared/Microsoft.WindowsDesktop.App/8.0.0-bootstrap.wpf.8.0.0/PresentationCore.dll"
env -u DOTNET_ROOT -u DOTNET_ROOT_X64 PATH=/usr/bin:/c/Windows/System32 \
  timeout 60 "$work/bin/Release/net8.0-windows/win-x64/publish/TablesSdkCheck.exe"
for directions in 'LeftToRight RightToLeft' 'RightToLeft LeftToRight'; do
  read -r page_direction table_direction <<< "$directions"
  env -u DOTNET_ROOT -u DOTNET_ROOT_X64 PATH=/usr/bin:/c/Windows/System32 \
    timeout 60 "$work/bin/Release/net8.0-windows/win-x64/publish/TablesSdkCheck.exe" "$page_direction" "$table_direction"
done
printf 'PASS normal offline WPF SDK table layout check and self-contained publish with SDK roots removed\n'
