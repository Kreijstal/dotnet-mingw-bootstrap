"""Apply the unchanged upstream zero-reference-version task to framework shims."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys,xml.etree.ElementTree as E

root=Path(sys.argv[1]).resolve();assembly=Path(sys.argv[2]).resolve()
here=Path(__file__).resolve().parent;source=root/'m/src/runtime-8.0.31-native'
names={s.strip() for s in E.parse(source/'src/libraries/NetCoreAppLibrary.props').findtext('.//NetFxReference').split(';') if s.strip()}
if assembly.stem not in names:sys.exit(0)
assert assembly.is_relative_to(root),assembly
assert '<GenFacadesForceZeroVersionSeeds Condition="$(NetFxReference.Contains(\'$(MSBuildProjectName);\'))">true</GenFacadesForceZeroVersionSeeds>' in (source/'src/libraries/shims/Directory.Build.props').read_text()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip()
inputs={name:sha(here/'inputs'/name) for name in ['GenFacades-BuildTask.cs','ClearAssemblyReferenceVersions.cs','GenFacades.ClearVersion.targets']}
assert inputs['GenFacades-BuildTask.cs']=='62cc755b3bfc635db520c4bad47d14c72d04a1de3acc22ffe1b22e5bca407572'
assert inputs['ClearAssemblyReferenceVersions.cs']=='946f05a407e5f919f55093a1ab7f532a4272a86d0e5cd8ca3a4bddcc4c45781b'
assert inputs['GenFacades.ClearVersion.targets']=='56ca6f12848f16d8a749c4ec1c6e81ceb767e1953c097484bfef979eb4bd77c3'
assert E.parse(source/'eng/Version.Details.xml').find(".//Dependency[@Name='Microsoft.DotNet.GenFacades']/Sha").text=='444c2ef3c414558e2e409ee346f1faebb19687eb'
out=root/'shim-version-task';out.mkdir(exist_ok=True)
env=dict(os.environ,DOTNET_ROOT=native(Path('/clang64/lib/dotnet')),DOTNET_ROOT_X64=native(Path('/clang64/lib/dotnet')),DOTNET_CLI_HOME=native(out/'cli-home'),NUGET_PACKAGES=native(out/'cache'),DOTNET_CLI_TELEMETRY_OPTOUT='1',MSBuildEnableWorkloadResolver='false')
dotnet='/clang64/lib/dotnet/dotnet.exe';task=out/'bin/Release/net8.0/SourceRuntime.ShimVersionTask.dll'
if not task.is_file():
 project=E.Element('Project',Sdk='Microsoft.NET.Sdk');props=E.SubElement(project,'PropertyGroup')
 for name,value in dict(TargetFramework='net8.0',EnableDefaultItems='false',EnableNETAnalyzers='false',NuGetAudit='false',AssemblyName='SourceRuntime.ShimVersionTask').items():E.SubElement(props,name).text=value
 items=E.SubElement(project,'ItemGroup')
 for name in ['GenFacades-BuildTask.cs','ClearAssemblyReferenceVersions.cs']:E.SubElement(items,'Compile',Include=native(here/'inputs'/name))
 for name in ['Microsoft.Build.Framework','Microsoft.Build.Utilities.Core']:
  item=E.SubElement(items,'Reference',Include=name);E.SubElement(item,'HintPath').text=native(Path('/clang64/lib/dotnet/sdk/8.0.131')/(name+'.dll'));E.SubElement(item,'Private').text='false'
 E.ElementTree(project).write(out/'Build.csproj',encoding='utf-8',xml_declaration=True)
 (out/'NuGet.config').write_text('<configuration><packageSources><clear/></packageSources></configuration>\n')
 with (out/'build.log').open('w') as log:subprocess.run([dotnet,'build',native(out/'Build.csproj'),'-c','Release','--configfile',native(out/'NuGet.config'),'-p:UseSharedCompilation=false','-v:minimal'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 (out/'source-inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
assert json.loads((out/'source-inputs.json').read_text())==inputs
project=E.Element('Project');E.SubElement(project,'UsingTask',TaskName='Microsoft.DotNet.GenFacades.ClearAssemblyReferenceVersions',AssemblyFile=native(task));target=E.SubElement(project,'Target',Name='Clear')
E.SubElement(target,'Microsoft.DotNet.GenFacades.ClearAssemblyReferenceVersions',Assembly=native(assembly))
E.ElementTree(project).write(out/'Clear.proj',encoding='utf-8',xml_declaration=True)
before=sha(assembly)
with (out/'clear.log').open('a') as log:subprocess.run([dotnet,'msbuild',native(out/'Clear.proj'),'-t:Clear','-v:minimal','-nologo'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
metadata=subprocess.check_output(['/ucrt64/bin/monodis','--assemblyref',str(assembly)],text=True)
versions=re.findall(r'Version=([^\s]+)',metadata);assert versions and set(versions)=={'0.0.0.0'},(assembly,versions)
path=root/'shim-version-audit.json';records=json.loads(path.read_text()) if path.is_file() else {}
records[assembly.relative_to(root).as_posix()]=dict(before_sha256=before,after_sha256=sha(assembly),original_task_inputs=inputs,assembly_reference_versions=versions)
path.write_text(json.dumps(records,indent=2)+'\n')
print('PASS original GenFacades zero-version policy:',assembly.name,len(versions),'assembly references')
