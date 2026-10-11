#!/bin/bash
# Run against an isolated source SDK root with the staged WinForms layout merged in.
set -euo pipefail
sdk=$1
work=$2
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$work"
cp "$here/Program.cs" "$work/Program.cs"
# Exercise the upstream application configuration generator too.
/usr/bin/python3 - "$work/Program.cs" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text();s=s.replace('Application.SetHighDpiMode(HighDpiMode.SystemAware);\n        Application.EnableVisualStyles();\n        Application.SetCompatibleTextRenderingDefault(false);','ApplicationConfiguration.Initialize();');p.write_text(s)
PY
cat > "$work/WinFormsSmoke.csproj" <<'PROJ'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0-windows</TargetFramework><OutputType>Exe</OutputType><UseWindowsForms>true</UseWindowsForms><ImplicitUsings>enable</ImplicitUsings><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><EnableUnsafeBinaryFormatterSerialization>true</EnableUnsafeBinaryFormatterSerialization></PropertyGroup></Project>
PROJ
printf '<configuration><packageSources><clear /></packageSources></configuration>\n' > "$work/NuGet.config"
export DOTNET_ROOT=$(cygpath -m "$sdk")
export DOTNET_ROOT_X64=$DOTNET_ROOT
export DOTNET_CLI_HOME=$(cygpath -m "$work/cli-home")
export NUGET_PACKAGES=$(cygpath -m "$work/empty-packages")
"$sdk/dotnet.exe" build "$(cygpath -m "$work/WinFormsSmoke.csproj")" --configfile "$(cygpath -m "$work/NuGet.config")" -v:q -p:NuGetAudit=false -p:UseSharedCompilation=false
# SDK must resolve the shared framework; runtime dependencies must not be app-local.
[ ! -e "$work/bin/Debug/net8.0-windows/System.Windows.Forms.dll" ]
timeout 60 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Debug/net8.0-windows/WinFormsSmoke.dll")"
"$sdk/dotnet.exe" publish "$(cygpath -m "$work/WinFormsSmoke.csproj")" -r win-x64 --self-contained true --configfile "$(cygpath -m "$work/NuGet.config")" -p:UseAppHost=true -v:q -p:NuGetAudit=false -p:UseSharedCompilation=false
[ -e "$work/bin/Release/net8.0-windows/win-x64/publish/System.Windows.Forms.dll" ]
PATH=/usr/bin:/c/Windows/System32 timeout 60 "$work/bin/Release/net8.0-windows/win-x64/publish/WinFormsSmoke.exe"
