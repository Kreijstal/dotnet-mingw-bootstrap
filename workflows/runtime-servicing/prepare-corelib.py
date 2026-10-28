"""Adapt pinned build helpers to installed source tools and freshly built generators."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys,xml.etree.ElementTree as E
root=Path(sys.argv[1]).resolve();base=root/'bootstrap/dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f';here=base/'bootstrap/m612';out=root/'managed-build'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip()
file=here/'corelib.sh';before=file.read_bytes();text=before.decode()
a='local o=$1 defs=$2 sdk=$M612/dotnet-sdk-8.0.131-net8 cl=$SRC/src/coreclr ill=$SRC/src/coreclr/System.Private.CoreLib/src/ILLink'
b='local o=$1 defs=$2 sdk=${RUNTIME_BUILD_SDK:?missing source SDK} cl=$SRC/src/coreclr ill=$SRC/src/coreclr/System.Private.CoreLib/src/ILLink'
assert text.count(a)==1;text=text.replace(a,b);replacements=[dict(before=a,after=b)]
a='local tasks; tasks=$(ls "$M612"/illink-$RT_VER-net8/microsoft.net.illink.tasks/*/tools/net8.0/ILLink.Tasks.dll | head -1)'
b='local tasks=${RUNTIME_ROOTS_TASK:?missing source runtime-root task}'
assert text.count(a)==1;text=text.replace(a,b);replacements.append(dict(before=a,after=b))
file.write_text(text)
record=dict(purpose='Build helper input paths only; no production C# changes',helper='bootstrap/m612/corelib.sh',original_sha256=hashlib.sha256(before).hexdigest(),prepared_sha256=sha(file),replacements=replacements)
(root/'corelib-build-adapter.json').write_text(json.dumps(record,indent=2)+'\n')
# Place the new generator outputs where the original CoreLib helper expects them.
folder=root/'m/pkgs/analyzers';folder.mkdir(parents=True)
for name in ['Microsoft.Interop.SourceGeneration','Microsoft.Interop.LibraryImportGenerator','Microsoft.Interop.ComInterfaceGenerator','System.Private.CoreLib.Generators']:
    origin=out/name/'bin/Release/net8.0'/(name+'.dll');assert origin.is_file();shutil.copyfile(origin,folder/origin.name)
# Build the exact original PE metadata postprocessor with the installed source SDK.
tool=out/'FcallRva';tool.mkdir();p=E.Element('Project',Sdk='Microsoft.NET.Sdk');props=E.SubElement(p,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',UseAppHost='false',EnableDefaultItems='false',EnableNETAnalyzers='false',NuGetAudit='false',AssemblyName='fcallrva',OutputPath=native(root/'m/tools'),AppendTargetFrameworkToOutputPath='false').items():E.SubElement(props,k).text=v
E.SubElement(E.SubElement(p,'ItemGroup'),'Compile',Include=native(here/'tools/fcallrva.cs'))
E.indent(p);E.ElementTree(p).write(tool/'Build.csproj',encoding='utf-8',xml_declaration=True)
print('PASS CoreLib helper uses source SDK/new runtime-root task and all freshly rebuilt servicing generators; original metadata tool staged')
