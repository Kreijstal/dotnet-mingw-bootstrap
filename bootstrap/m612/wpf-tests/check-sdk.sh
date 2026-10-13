#!/bin/bash
set -euo pipefail
sdk=$1
work=$2
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$work"
cp "$here/SdkApp.xaml" "$work/App.xaml"
cp "$here/SdkWindow.xaml" "$work/MainWindow.xaml"
cp "$here/SdkWindow.xaml.cs" "$work/MainWindow.xaml.cs"
cat > "$work/WpfSdkCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0-windows</TargetFramework><OutputType>WinExe</OutputType><UseWPF>true</UseWPF><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>
EOF
printf '<configuration><packageSources><clear /></packageSources></configuration>\n' > "$work/NuGet.config"
export DOTNET_ROOT=$(cygpath -m "$sdk")
export DOTNET_ROOT_X64=$DOTNET_ROOT
export DOTNET_CLI_HOME=$(cygpath -m "$work/cli-home")
export NUGET_PACKAGES=$(cygpath -m "$work/empty-packages")
"$sdk/dotnet.exe" build "$(cygpath -m "$work/WpfSdkCheck.csproj")" --configfile "$(cygpath -m "$work/NuGet.config")" -v:q -p:NuGetAudit=false -p:UseSharedCompilation=false
test -s "$work/obj/Debug/net8.0-windows/MainWindow.baml"
test ! -e "$work/bin/Debug/net8.0-windows/PresentationFramework.dll"
timeout 60 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Debug/net8.0-windows/WpfSdkCheck.dll")"
"$sdk/dotnet.exe" publish "$(cygpath -m "$work/WpfSdkCheck.csproj")" -r win-x64 --self-contained true \
  --configfile "$(cygpath -m "$work/NuGet.config")" -p:UseAppHost=true -v:q -p:NuGetAudit=false -p:UseSharedCompilation=false
test -s "$work/bin/Release/net8.0-windows/win-x64/publish/PresentationFramework.dll"
env -u DOTNET_ROOT -u DOTNET_ROOT_X64 PATH=/usr/bin:/c/Windows/System32 timeout 60 "$work/bin/Release/net8.0-windows/win-x64/publish/WpfSdkCheck.exe"
