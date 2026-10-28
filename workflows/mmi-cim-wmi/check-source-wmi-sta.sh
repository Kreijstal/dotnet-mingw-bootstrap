#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
mkdir "${1:?usage: check-source-wmi-sta.sh NEW_OUTPUT}"
out=$(cd "$1" && pwd)
cp "$here/../../mingw-w64-dotnet-management/PortChecks.cs" "$out/"
cat > "$out/Program.cs" <<'CS'
using System;
using System.Threading;
internal static class Program {
 [STAThread] static int Main() {
  try {
   if(Thread.CurrentThread.GetApartmentState()!=ApartmentState.STA)throw new Exception("STA entry point required");
   PortChecks.Run();
   Console.WriteLine("PASS full source WMI metadata/method/async/event/privilege/native suite from STA caller");
   return 0;
  } catch(Exception error) {Console.Error.WriteLine(error);return 1;}
 }
}
CS
cat > "$out/check.csproj" <<'XML'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableDefaultItems>false</EnableDefaultItems><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Compile Include="Program.cs"/><Compile Include="PortChecks.cs"/><PackageReference Include="System.Management" Version="[8.0.0]"/></ItemGroup></Project>
XML
cat > "$out/NuGet.config" <<XML
<configuration><packageSources><clear/><add key="installed-source" value="$(cygpath -am /clang64/lib/dotnet/library-packs)"/></packageSources></configuration>
XML
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_ROOT_X64=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/cache")
export MSBuildEnableWorkloadResolver=false DOTNET_CLI_TELEMETRY_OPTOUT=1
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/check.csproj")" -c Release \
 --configfile "$(cygpath -am "$out/NuGet.config")" -p:UseSharedCompilation=false -v:minimal
/usr/bin/python3 - "$out" <<'PY'
from pathlib import Path
import sys,zipfile
out=Path(sys.argv[1]);feed=Path('/clang64/lib/dotnet/library-packs/system.management.8.0.0.nupkg')
assert (out/'cache/system.management/8.0.0/system.management.8.0.0.nupkg').read_bytes()==feed.read_bytes()
with zipfile.ZipFile(feed) as archive:
 for name in ['runtimes/win/lib/net8.0/System.Management.dll','runtimes/win-x64/native/msys-wmi-native.dll']:
  assert (out/'bin/Release/net8.0'/name).read_bytes()==archive.read(name),name
print('PASS exact installed source archive and managed/native asset bytes')
PY
timeout -k 10 100 /clang64/lib/dotnet/dotnet.exe "$(cygpath -am "$out/bin/Release/net8.0/check.dll")"
