#!/usr/bin/env bash
# Use a private copy of the installed source SDK on an already staged runtime.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
layout=$(cd "${1:?usage: check-sdk.sh RUNTIME_LAYOUT NEW_OUTPUT}" && pwd)
mkdir "${2:?missing new output}"; out=$(cd "$2" && pwd)
mkdir "$out/sdk-root"
cp "$layout/dotnet.exe" "$out/sdk-root/"
cp -R "$layout/host" "$layout/shared" "$out/sdk-root/"
cp -R /clang64/lib/dotnet/sdk /clang64/lib/dotnet/packs /clang64/lib/dotnet/compiler-packs "$out/sdk-root/"
export PATH=/clang64/bin:/usr/bin:$PATH
export DOTNET_ROOT=$(cygpath -am "$out/sdk-root")
export DOTNET_ROOT_X64=$DOTNET_ROOT DOTNET_MULTILEVEL_LOOKUP=0 DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/cache")
export DOTNET_MSBUILD_SDK_RESOLVER_CLI_DIR=$DOTNET_ROOT
mkdir "$out/consumer"
cp "$here/SdkGeneratorCheck.cs" "$out/consumer/Program.cs"
cat > "$out/consumer/Check.csproj" <<'XML'
<Project Sdk="Microsoft.NET.Sdk">
 <PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><AllowUnsafeBlocks>true</AllowUnsafeBlocks><Nullable>enable</Nullable><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit><TreatWarningsAsErrors>true</TreatWarningsAsErrors><EmitCompilerGeneratedFiles>true</EmitCompilerGeneratedFiles></PropertyGroup>
 <UsingTask TaskName="OwnedServicedBuildProbe" TaskFactory="RoslynCodeTaskFactory" AssemblyFile="$(MSBuildToolsPath)/Microsoft.Build.Tasks.Core.dll">
  <Task><Using Namespace="System"/><Code Type="Fragment" Language="cs"><![CDATA[
   Log.LogMessage(Microsoft.Build.Framework.MessageImportance.High,"MSBUILD RUNTIME " + Environment.Version + " CORELIB " + typeof(object).Assembly.Location);
   if (Environment.Version != new Version(8,0,31)) { Log.LogError("MSBuild is running on an unserviced runtime"); return false; }
  ]]></Code></Task>
 </UsingTask>
 <Target Name="CheckActualMSBuildRuntime" BeforeTargets="BeforeBuild"><OwnedServicedBuildProbe/></Target>
</Project>
XML
cat > "$out/consumer/NuGet.config" <<XML
<configuration><packageSources><clear/><add key="installed-source" value="$(cygpath -am /clang64/lib/dotnet/library-packs)"/></packageSources></configuration>
XML
"$out/sdk-root/dotnet.exe" --info
"$out/sdk-root/dotnet.exe" build "$(cygpath -am "$out/consumer/Check.csproj")" -c Release --configfile "$(cygpath -am "$out/consumer/NuGet.config")" -p:UseSharedCompilation=false -v:minimal
timeout 90 "$out/sdk-root/dotnet.exe" "$(cygpath -am "$out/consumer/bin/Release/net8.0/Check.dll")"
/usr/bin/python3 - "$out/consumer/obj/Release/net8.0/generated" <<'PY'
from pathlib import Path
import sys
root=Path(sys.argv[1]);files=list(root.rglob('*.cs'))
for name in ['LibraryImportGenerator','RegexGenerator','JsonSourceGenerator']:
 assert any(name in p.as_posix() for p in files),(name,[str(p) for p in files])
print('PASS real compiler-generated LibraryImport, Regex and JSON output files:',len(files))
PY
