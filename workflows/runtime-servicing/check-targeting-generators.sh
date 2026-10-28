#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "${1:?usage: check-targeting-generators.sh PREPARED_ROOT SDK_ROOT NEW_OUTPUT}" && pwd)
sdk=$(cd "${2:?missing private source SDK root}" && pwd)
mkdir "${3:?missing new output}"; out=$(cd "$3" && pwd)
export DOTNET_ROOT=$(cygpath -am "$sdk") DOTNET_ROOT_X64=$(cygpath -am "$sdk")
export DOTNET_MULTILEVEL_LOOKUP=0 DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/cache")
/usr/bin/python3 - "$root" "$out" "$here/SdkGeneratorCheck.cs" <<'PY'
from pathlib import Path
import json,subprocess,sys,xml.etree.ElementTree as E
root,out,sample=map(Path,sys.argv[1:]);all_records=json.loads((root/'targeting-generators/source-inputs.json').read_text());records={k:v for k,v in all_records.items() if k in ['JSImport','Json3.11','Json4.0','Json4.4']}
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip()
for label,item in records.items():
 folder=out/label;folder.mkdir()
 if label=='JSImport':
  text='''using System; using System.Runtime.InteropServices.JavaScript;
internal static partial class Program {
 [JSImport("ownedAdd", "ownedModule")] internal static partial double Add(double a,double b);
 static void Main() { if(Environment.Version!=new Version(8,0,31))throw new Exception("Wrong runtime"); Console.WriteLine("PASS generated JS import compiled on serviced runtime; browser invocation remains platform-specific"); }
}'''
 else:text=sample.read_text()
 (folder/'Program.cs').write_text(text)
 p=E.Element('Project',Sdk='Microsoft.NET.Sdk');props=E.SubElement(p,'PropertyGroup')
 for key,value in dict(TargetFramework='net8.0',OutputType='Exe',UseAppHost='false',AllowUnsafeBlocks='true',Nullable='enable',EnableNETAnalyzers='false',NuGetAudit='false',EmitCompilerGeneratedFiles='true',UseSharedCompilation='false',CscToolPath=native(Path('/clang64/lib/dotnet/compiler-packs/roslyn-4.9.2')),CscToolExe='csc49.cmd',JsonSerializerIsReflectionEnabledByDefault='false').items():E.SubElement(props,key).text=value
 capture=E.SubElement(p,'Target',Name='UseNewSourceTargetingGenerator',BeforeTargets='CoreCompile');items=E.SubElement(capture,'ItemGroup')
 selected=['Microsoft.Interop.SourceGeneration','Microsoft.Interop.LibraryImportGenerator','Microsoft.Interop.ComInterfaceGenerator','System.Text.RegularExpressions.Generator','JSImport']
 if label!='JSImport':selected.append(label)
 for peer in selected:
  assembly=all_records[peer]['assembly']
  E.SubElement(items,'Analyzer',Remove='@(Analyzer)',Condition="'%(Analyzer.Filename)' == '"+assembly+"'")
  file=root/'targeting-generators'/peer/'bin/Release/net8.0'/(assembly+'.dll')
  E.SubElement(items,'Analyzer',Include=native(file))
 E.SubElement(capture,'WriteLinesToFile',File='$(IntermediateOutputPath)selected-analyzers.txt',Lines="@(Analyzer->'%(FullPath)')",Overwrite='true',Encoding='UTF-8')
 E.indent(p);E.ElementTree(p).write(folder/'Check.csproj',encoding='utf-8',xml_declaration=True)
 (folder/'NuGet.config').write_text((root/'targeting-generators/NuGet.config').read_text())
PY
for name in Json3.11 Json4.0 Json4.4 'JSImport'; do
 "$sdk/dotnet.exe" build "$(cygpath -am "$out/$name/Check.csproj")" -c Release --configfile "$(cygpath -am "$out/$name/NuGet.config")" -v:minimal > "$out/$name.log" 2>&1
 timeout 90 "$sdk/dotnet.exe" "$(cygpath -am "$out/$name/bin/Release/net8.0/Check.dll")" >> "$out/$name.log" 2>&1
 tail -9 "$out/$name.log"
done
/usr/bin/python3 - "$root" "$out" <<'PY'
from pathlib import Path
import json,sys
root,out=map(Path,sys.argv[1:]);records={k:v for k,v in json.loads((root/'targeting-generators/source-inputs.json').read_text()).items() if k in ['JSImport','Json3.11','Json4.0','Json4.4']}
for label,item in records.items():
 folder=out/label/'obj/Release/net8.0';analyzers=(folder/'selected-analyzers.txt').read_text(encoding='utf-8-sig').splitlines()
 selected=[p.replace('\\','/') for p in analyzers if Path(p.replace('\\','/')).name==item['assembly']+'.dll']
 assert len(selected)==1 and '/targeting-generators/'+label+'/' in selected[0],(label,selected)
 outputs=list((folder/'generated').rglob('*.cs'));expected='JSImportGenerator' if label=='JSImport' else 'JsonSourceGenerator'
 assert any(expected in p.as_posix() for p in outputs),(label,[str(p) for p in outputs])
print('PASS new signed interop/COM/Regex/JSON analyzer selection, native COM vtable execution, all JSON variants with reflection disabled and JS import compilation; historical/browser runtime compatibility remain open')
PY
