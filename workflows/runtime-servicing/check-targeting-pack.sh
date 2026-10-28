#!/usr/bin/env bash
# Test FrameworkList/PlatformManifest discovery in an owned SDK copy.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
pack=$(cd "${1:?usage: check-targeting-pack.sh STAGED_PACK PRIVATE_SDK NEW_OUTPUT}" && pwd)
sdk=$(cd "${2:?missing private source SDK}" && pwd)
mkdir "${3:?missing new output}"; out=$(cd "$3" && pwd)
test "$sdk" != /clang64/lib/dotnet
mv "$sdk/packs/Microsoft.NETCore.App.Ref/8.0.31" "$out/original-refpack"
cp -R "$pack" "$sdk/packs/Microsoft.NETCore.App.Ref/8.0.31"
mkdir "$out/consumer"
cp "$here/SdkGeneratorCheck.cs" "$out/consumer/Program.cs"
cat > "$out/consumer/Check.csproj" <<'XML'
<Project Sdk="Microsoft.NET.Sdk">
 <PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><AllowUnsafeBlocks>true</AllowUnsafeBlocks><Nullable>enable</Nullable><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit><EmitCompilerGeneratedFiles>true</EmitCompilerGeneratedFiles><JsonSerializerIsReflectionEnabledByDefault>false</JsonSerializerIsReflectionEnabledByDefault><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup>
 <Target Name="RecordNormalPackSelection" BeforeTargets="CoreCompile">
  <WriteLinesToFile File="$(IntermediateOutputPath)analyzers.txt" Lines="@(Analyzer->'%(FullPath)')" Overwrite="true" Encoding="UTF-8"/>
  <WriteLinesToFile File="$(IntermediateOutputPath)references.txt" Lines="@(ReferencePathWithRefAssemblies->'%(FullPath)')" Overwrite="true" Encoding="UTF-8"/>
  <WriteLinesToFile File="$(IntermediateOutputPath)compiler.txt" Lines="$(CscToolPath)|$(CscToolExe)" Overwrite="true" Encoding="UTF-8"/>
 </Target>
</Project>
XML
cat > "$out/consumer/NuGet.config" <<XML
<configuration><packageSources><clear/><add key="installed-source" value="$(cygpath -am /clang64/lib/dotnet/library-packs)"/></packageSources></configuration>
XML
export DOTNET_ROOT=$(cygpath -am "$sdk") DOTNET_ROOT_X64=$(cygpath -am "$sdk")
export DOTNET_MULTILEVEL_LOOKUP=0 DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/cache")
"$sdk/dotnet.exe" build "$(cygpath -am "$out/consumer/Check.csproj")" -c Release --configfile "$(cygpath -am "$out/consumer/NuGet.config")" \
 -p:UseSharedCompilation=false -v:minimal
timeout 90 "$sdk/dotnet.exe" "$(cygpath -am "$out/consumer/bin/Release/net8.0/Check.dll")"
/usr/bin/python3 - "$out/consumer/obj/Release/net8.0" "$sdk" <<'PY'
from pathlib import Path
import hashlib,json,subprocess,sys
folder,sdk=map(Path,sys.argv[1:]);pack=sdk/'packs/Microsoft.NETCore.App.Ref/8.0.31';manifest=json.loads((pack/'source-payload.json').read_text())
prefix=subprocess.check_output(['cygpath','-am',str(pack)],text=True).strip().replace('\\','/')+'/'
analyzers=(folder/'analyzers.txt').read_text(encoding='utf-8-sig').splitlines();references=(folder/'references.txt').read_text(encoding='utf-8-sig').splitlines()
expected=['Microsoft.Interop.SourceGeneration','Microsoft.Interop.LibraryImportGenerator','Microsoft.Interop.ComInterfaceGenerator','Microsoft.Interop.JavaScript.JSImportGenerator','System.Text.RegularExpressions.Generator','System.Text.Json.SourceGeneration']
compiler=(folder/'compiler.txt').read_text(encoding='utf-8-sig').replace('\\','/').strip()
assert 'compiler-packs/roslyn-4.9.2' in compiler and compiler.endswith('|csc49.cmd'),compiler
assert hashlib.sha256((pack/'data/PackageOverrides.txt').read_bytes()).hexdigest()==manifest['original_package_overrides_sha256']
for name in expected:
 selected=[line.replace('\\','/') for line in analyzers if Path(line.replace('\\','/')).name==name+'.dll']
 assert len(selected)==1 and selected[0].lower().startswith(prefix.lower()),(name,selected)
 relative=selected[0][len(prefix):];assert hashlib.sha256((pack/relative).read_bytes()).hexdigest()==manifest['files'][relative]['sha256']
assert len(references)==163,len(references)
for line in references:
 line=line.replace('\\','/');assert line.lower().startswith(prefix.lower()),line
 relative=line[len(prefix):];assert hashlib.sha256((pack/relative).read_bytes()).hexdigest()==manifest['files'][relative]['sha256']
print('PASS SDK selects all163 actual serviced source refs and six rebuilt signed analyzers via normal targeting-pack metadata; no Analyzer injection')
PY
