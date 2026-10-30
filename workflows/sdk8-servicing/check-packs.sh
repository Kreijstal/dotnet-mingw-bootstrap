#!/usr/bin/env bash
# Exercise default SDK host selection and self-contained publishing with fresh caches.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
sdk=$(cd "${1:?usage: check-packs.sh SDK_ROOT NEW_OUTPUT}" && pwd)
mkdir "${2:?missing output}"; out=$(cd "$2" && pwd)
prefix=${MINGW_PREFIX:-/clang64}
doc=$prefix/share/doc/dotnet-runtime8-servicing
export PATH=$prefix/bin:/usr/bin:$PATH
export DOTNET_ROOT=$(cygpath -am "$sdk") DOTNET_ROOT_X64=$(cygpath -am "$sdk")
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/cache")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 DOTNET_MULTILEVEL_LOOKUP=0 MSBuildEnableWorkloadResolver=false
export DOTNET_MSBUILD_SDK_RESOLVER_CLI_DIR=$DOTNET_ROOT
dotnet=$sdk/dotnet.exe
cp "$doc/RuntimeCheck.cs" "$out/Program.cs"
/usr/bin/python3 - "$doc/FrameworkForwarderCheck.cs" "$out" <<'PY'
from pathlib import Path
import sys
source,out=Path(sys.argv[1]),Path(sys.argv[2]);text=source.read_text()
assert text.count('static void Main(string[] args)')==1
(out/'Forwarders.cs').write_text(text.replace('static void Main(string[] args)','internal static void Verify(string[] args)'))
PY
cat > "$out/PackChecks.cs" <<'CS'
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.Versioning;
[assembly: SupportedOSPlatform("windows")]
internal static class PackChecks
{
    [ModuleInitializer]
    internal static void CheckForwarders() => FrameworkForwarderCheck.Verify(new[] { Path.GetDirectoryName(typeof(object).Assembly.Location) });
}
CS
cat > "$out/Check.csproj" <<'XML'
<Project Sdk="Microsoft.NET.Sdk">
 <PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><EnableDefaultItems>false</EnableDefaultItems><Nullable>enable</Nullable><NuGetAudit>false</NuGetAudit></PropertyGroup>
 <ItemGroup><Compile Include="Program.cs;Forwarders.cs;PackChecks.cs"/>
  <PackageReference Include="System.Data.SqlClient" Version="[4.9.1]"/>
  <PackageReference Include="System.IO.Packaging" Version="[8.0.1]"/>
  <PackageReference Include="System.Security.Cryptography.Pkcs" Version="[8.0.1]"/>
  <PackageReference Include="System.Security.Cryptography.ProtectedData" Version="[8.0.0]"/>
  <PackageReference Include="System.ServiceProcess.ServiceController" Version="[8.0.1]"/>
  <PackageReference Include="System.Threading.AccessControl" Version="[8.0.0]"/>
 </ItemGroup>
 <Target Name="RecordDefaultSdkCompiler" BeforeTargets="CoreCompile">
  <WriteLinesToFile File="$(IntermediateOutputPath)compiler.txt" Lines="$(CscToolPath)|$(CscToolExe)" Overwrite="true"/>
  <WriteLinesToFile File="$(IntermediateOutputPath)analyzers.txt" Lines="@(Analyzer->'%(FullPath)')" Overwrite="true"/>
  <WriteLinesToFile File="$(IntermediateOutputPath)references.txt" Lines="@(ReferencePathWithRefAssemblies->'%(FullPath)')" Overwrite="true"/>
 </Target>
 <Target Name="RecordDefaultHost" BeforeTargets="_CreateAppHost">
  <WriteLinesToFile File="$(IntermediateOutputPath)apphost-source.txt" Lines="$(AppHostSourcePath)" Overwrite="true"/>
 </Target>
</Project>
XML
cat > "$out/NuGet.config" <<XML
<configuration><packageSources><clear/><add key="installed-source" value="$(cygpath -am "$prefix/lib/dotnet/library-packs")"/></packageSources></configuration>
XML
"$dotnet" build "$(cygpath -am "$out/Check.csproj")" -c Release --configfile "$(cygpath -am "$out/NuGet.config")" -p:UseSharedCompilation=false -v:minimal
timeout 180 "$out/bin/Release/net8.0/Check.exe" "$(cygpath -am "$sdk/shared/Microsoft.NETCore.App/8.0.31")" "$(cygpath -am "$doc/required-framework.txt")"
"$dotnet" publish "$(cygpath -am "$out/Check.csproj")" -c Release -r win-x64 --self-contained true \
 --configfile "$(cygpath -am "$out/NuGet.config")" -p:UseSharedCompilation=false -o "$(cygpath -am "$out/published")" -v:minimal
(
 export DOTNET_ROOT=$(cygpath -am "$out/no-global-runtime") DOTNET_ROOT_X64=$(cygpath -am "$out/no-global-runtime")
 timeout 180 "$out/published/Check.exe" "$(cygpath -am "$out/published")" "$(cygpath -am "$doc/required-framework.txt")"
)
/usr/bin/python3 "$here/audit-packs-check.py" "$sdk" "$out"
echo 'PASS full168 managed/native runtime and six source-NuGet forwarders in ordinary apphost and self-contained execution'
