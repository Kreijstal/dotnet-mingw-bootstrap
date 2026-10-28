"""Prepare full JS/JSON generators for the source .NET8/Roslyn4.9 build host.

Original project imports, conditions and C# bytes are retained. This does not
claim netstandard2.0 or historical Roslyn host compatibility.
"""
from pathlib import Path
import hashlib,json,os,sys,subprocess,xml.etree.ElementTree as E

root=Path(sys.argv[1]).resolve();out=root/'targeting-generators';out.mkdir(exist_ok=True)
source=root/'m/src/runtime-8.0.31-native';libs=source/'src/libraries';common=libs/'Common/src';core=libs/'System.Private.CoreLib/src'
key_file=next((root/'bootstrap/dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f/bootstrap/m612/keys').glob('Open-*.pubkey.hex'))
key=bytes.fromhex(key_file.read_text());assert hashlib.sha1(key).digest()[-8:][::-1].hex()=='cc7b13ffcd2ddd51'
(out/'Open.public.snk').write_bytes(key)
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
projects={'JSImport':libs/'System.Runtime.InteropServices.JavaScript/gen/JSImportGenerator/JSImportGenerator.csproj'}
projects.update({'Json'+v:libs/('System.Text.Json/gen/System.Text.Json.SourceGeneration.Roslyn'+v+'.csproj') for v in ['3.11','4.0','4.4']})
conditions={None:True,"'$(TargetFrameworkIdentifier)' != '.NETCoreApp'":False,"'$(TargetFrameworkIdentifier)' == '.NETCoreApp'":True,
 "'$(TargetFrameworkIdentifier)' == '.NETCoreApp' and $([MSBuild]::VersionGreaterThanOrEquals('$(TargetFrameworkVersion)', '7.0'))":True,
 "!$([MSBuild]::IsTargetFrameworkCompatible('$(TargetFramework)', 'net7.0'))":False,
 "!$([MSBuild]::IsTargetFrameworkCompatible('$(TargetFramework)', 'net8.0'))":False,
 "$([MSBuild]::IsTargetFrameworkCompatible('$(TargetFramework)', 'net8.0'))":True}
records={}
for label,original in projects.items():
 folder=out/label;folder.mkdir(exist_ok=True);name='Microsoft.Interop.JavaScript.JSImportGenerator' if label=='JSImport' else 'System.Text.Json.SourceGeneration'
 expected={};metadata={};translations={};rel=lambda p:os.path.relpath(p,folder).replace('\\','/')
 def expand(value,base):
  value=value.replace('\\','/').replace('$(CommonPath)',str(common)+'/').replace('$(CoreLibSharedDir)',str(core)+'/').replace('$(LibrariesProjectRoot)',str(libs)+'/')
  assert '$(' not in value,value
  return (base/value).resolve()
 def rewrite(file):
  tree=E.parse(file);tree.getroot().attrib.pop('Sdk',None);metadata[file.relative_to(source).as_posix()]=sha(file)
  for group in tree.getroot():
   if group.tag=='Import':
    imported=expand(group.get('Project'),file.parent)
    assert imported.name=='System.Text.Json.SourceGeneration.targets',imported
    nested=rewrite(imported);E.indent(nested);nested.write(folder/'upstream.targets',encoding='utf-8',xml_declaration=True)
    group.set('Project','upstream.targets');continue
   if group.tag!='ItemGroup':continue
   for item in list(group):
    if item.tag=='Compile' and item.get('Include'):
     path=expand(item.get('Include'),file.parent);assert path.is_file(),path
     condition=group.get('Condition');assert condition in conditions,condition
     assert item.get('Condition') is None,item.attrib
     if conditions[condition]:expected[path.relative_to(source).as_posix()]=sha(path)
     item.set('Include',rel(path))
    elif item.tag in ['EmbeddedResource','None','Content','Folder']:
     for attribute in ['Include','Update','Remove']:
      if item.get(attribute):item.set(attribute,rel(expand(item.get(attribute),file.parent)))
    elif item.tag=='PackageReference':
     package=item.get('Include');assert package in ['Microsoft.CodeAnalysis.CSharp.Workspaces','Microsoft.CodeAnalysis.Analyzers'],package
     item.set('Version','[3.3.4]' if package.endswith('.Analyzers') else '[4.9.2]')
    elif item.tag=='ProjectReference':
     peer=expand(item.get('Include'),file.parent)
     assert peer==libs/'System.Runtime.InteropServices/gen/Microsoft.Interop.SourceGeneration/Microsoft.Interop.SourceGeneration.csproj',peer
     group.remove(item)
  return tree
 upstream=rewrite(original);E.indent(upstream);upstream.write(folder/'upstream.proj',encoding='utf-8',xml_declaration=True)
 project=E.Element('Project',Sdk='Microsoft.NET.Sdk');props=E.SubElement(project,'PropertyGroup')
 for k,v in dict(ImportDirectoryBuildProps='false',ImportDirectoryBuildTargets='false',EnableDefaultItems='false',TargetFramework='net8.0',NetCoreAppToolCurrent='net8.0',TargetFrameworkIdentifier='.NETCoreApp',TargetFrameworkVersion='v8.0',CommonPath=native(common)+'/',CoreLibSharedDir=native(core)+'/',LibrariesProjectRoot=native(libs)+'/').items():E.SubElement(props,k).text=v
 E.SubElement(project,'Import',Project='upstream.proj');props=E.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',TargetFrameworks='',AssemblyName=name,RootNamespace=name,Version='8.0.31',AssemblyVersion='8.0.0.0',FileVersion=(root/'file-version.txt').read_text().strip(),InformationalVersion='8.0.31+1219a42122cf5190c7f512850557e38c422430dc',IncludeSourceRevisionInInformationalVersion='false',EnableDefaultItems='false',EnableNETAnalyzers='false',NuGetAudit='false',Nullable='enable',CopyLocalLockFileAssemblies='true',SignAssembly='true',PublicSign='true',AssemblyOriginatorKeyFile=native(out/'Open.public.snk')).items():E.SubElement(props,k).text=v
 items=E.SubElement(project,'ItemGroup')
 if label=='JSImport':
  for path in sorted(original.parent.rglob('*.cs')):
   expected[path.relative_to(source).as_posix()]=sha(path);E.SubElement(items,'Compile',Include=rel(path))
  reference=E.SubElement(items,'Reference',Include='Microsoft.Interop.SourceGeneration')
  E.SubElement(reference,'HintPath').text=native(root/'managed-build/Microsoft.Interop.SourceGeneration/bin/Release/net8.0/Microsoft.Interop.SourceGeneration.dll')
 common_sr=common/'System/SR.cs';expected[common_sr.relative_to(source).as_posix()]=sha(common_sr)
 E.SubElement(items,'Compile',Include=rel(common_sr));E.SubElement(items,'Compile',Include='SR.Generated.cs')
 resource=original.parent/'Resources/Strings.resx';metadata[resource.relative_to(source).as_posix()]=sha(resource)
 anchor='FxResources.'+name+'.SR';values={e.get('name'):e.findtext('value') or '' for e in E.parse(resource).iter('data')}
 code=['namespace '+anchor.rsplit('.',1)[0]+' { internal static class SR {} }','namespace System { internal static partial class SR {','internal static global::System.Resources.ResourceManager ResourceManager { get; } = new global::System.Resources.ResourceManager("'+anchor+'", typeof('+anchor+').Assembly);']
 for key in values:
  assert key.isidentifier(),key
  code.append('internal static string '+key+' => GetResourceString("'+key+'");')
 code.append('}}');(folder/'SR.Generated.cs').write_text('\n'.join(code)+'\n')
 E.SubElement(items,'EmbeddedResource',Include=native(resource),LogicalName=anchor+'.resources')
 namespace={'x':'urn:oasis:names:tc:xliff:document:1.2'}
 for xlf in sorted((resource.parent/'xlf').glob('Strings.*.xlf')):
  translated=E.parse(xlf);culture=translated.find('x:file',namespace).get('target-language');strings=dict(values)
  for unit in translated.findall('.//x:trans-unit',namespace):
   target=unit.find('x:target',namespace)
   if target is not None:
    assert unit.get('id') in strings,unit.get('id')
    strings[unit.get('id')]=''.join(target.itertext())
  localized=E.parse(resource)
  for entry in localized.iter('data'):entry.find('value').text=strings[entry.get('name')]
  filename='Strings.'+culture+'.resx';localized.write(folder/filename,encoding='utf-8',xml_declaration=True)
  E.SubElement(items,'EmbeddedResource',Include=filename,LogicalName=anchor+'.'+culture+'.resources',Culture=culture,WithCulture='true')
  metadata[xlf.relative_to(source).as_posix()]=sha(xlf);translations[culture]=strings
 if label.startswith('Json'):
  exclude=E.SubElement(project,'Target',Name='HonorOriginalFrameworkReferenceExclusion',AfterTargets='ResolveTargetingPackAssets')
  E.SubElement(E.SubElement(exclude,'ItemGroup'),'Reference',Remove='@(Reference)',Condition="'%(Reference.Filename)' == 'System.Text.Json'")
 capture=E.SubElement(project,'Target',Name='CaptureTargetingGeneratorInputs',BeforeTargets='CoreCompile')
 if label.startswith('Json'):
  remove=E.SubElement(capture,'ItemGroup')
  for reference_kind in ['ReferencePath','ReferencePathWithRefAssemblies']:
   E.SubElement(remove,reference_kind,Remove='@('+reference_kind+')',Condition="'%("+reference_kind+".Filename)' == 'System.Text.Json'")
 for kind in ['Compile','ReferencePath','ReferencePathWithRefAssemblies','Analyzer']:E.SubElement(capture,'WriteLinesToFile',File='$(IntermediateOutputPath)'+kind+'.txt',Lines='@('+kind+"->'%(FullPath)')",Overwrite='true',Encoding='UTF-8')
 E.indent(project);E.ElementTree(project).write(folder/'Build.csproj',encoding='utf-8',xml_declaration=True)
 records[label]=dict(assembly=name,original_project=original.relative_to(source).as_posix(),original_compiler_inputs=expected,original_project_resource_hashes=metadata,neutral_strings=values,translated_strings=translations,production_cs_changes=[],retargeted_host='net8.0/source Roslyn4.9.2; not a claim of historical/netstandard generator host compatibility')
(out/'source-inputs.json').write_text(json.dumps(records,ensure_ascii=False,indent=2)+'\n')
(out/'NuGet.config').write_text('<configuration><packageSources><clear/><add key="installed-source" value="'+native(Path('/clang64/lib/dotnet/library-packs'))+'"/></packageSources></configuration>\n')
print('PASS original JS/JSON generator selections and neutral/XLF resources:',{k:len(v['original_compiler_inputs']) for k,v in records.items()})
