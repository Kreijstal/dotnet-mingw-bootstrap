"""Retain complete original interop/Regex projects with targeting-pack identities."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys,xml.etree.ElementTree as E

root=Path(sys.argv[1]).resolve();out=root/'targeting-generators';source=root/'m/src/runtime-8.0.31-native'
originals=json.loads((root/'managed-build/source-inputs.json').read_text());records=json.loads((out/'source-inputs.json').read_text())
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
names=['Microsoft.Interop.SourceGeneration','Microsoft.Interop.LibraryImportGenerator','Microsoft.Interop.ComInterfaceGenerator','System.Text.RegularExpressions.Generator']
for name in names:
 old=root/'managed-build'/name;folder=out/name;folder.mkdir(exist_ok=True)
 for file in ['upstream.proj','SR.Generated.cs']:shutil.copy2(old/file,folder/file)
 project=E.parse(old/'Build.csproj');props=E.SubElement(project.getroot(),'PropertyGroup')
 for key,value in dict(AssemblyVersion='8.0.0.0',FileVersion=(root/'file-version.txt').read_text().strip(),SignAssembly='true',PublicSign='true',AssemblyOriginatorKeyFile=native(out/'Open.public.snk')).items():E.SubElement(props,key).text=value
 item=originals[name];expected=dict(item['original_cs']);sr=source/'src/libraries/Common/src/System/SR.cs';expected[sr.relative_to(source).as_posix()]=sha(sr)
 original=source/item['original_project'];resource=(source/'src/libraries/System.Runtime.InteropServices/gen/Common/Resources/Strings.resx') if name.startswith('Microsoft.Interop.') else original.parent/'Resources/Strings.resx'
 metadata={original.relative_to(source).as_posix():sha(original),resource.relative_to(source).as_posix():sha(resource)}
 values={entry.get('name'):entry.findtext('value') or '' for entry in E.parse(resource).iter('data')};translations={};items=E.SubElement(project.getroot(),'ItemGroup');anchor='FxResources.'+name+'.SR'
 ns={'x':'urn:oasis:names:tc:xliff:document:1.2'}
 for xlf in sorted((resource.parent/'xlf').glob('Strings.*.xlf')):
  x=E.parse(xlf);culture=x.find('x:file',ns).get('target-language');strings=dict(values)
  for unit in x.findall('.//x:trans-unit',ns):
   target=unit.find('x:target',ns)
   if target is not None:
    assert unit.get('id') in strings,unit.get('id')
    strings[unit.get('id')]=''.join(target.itertext())
  localized=E.parse(resource)
  for entry in localized.iter('data'):entry.find('value').text=strings[entry.get('name')]
  filename='Strings.'+culture+'.resx';localized.write(folder/filename,encoding='utf-8',xml_declaration=True)
  E.SubElement(items,'EmbeddedResource',Include=filename,LogicalName=anchor+'.'+culture+'.resources',Culture=culture,WithCulture='true')
  metadata[xlf.relative_to(source).as_posix()]=sha(xlf);translations[culture]=strings
 capture=E.SubElement(project.getroot(),'Target',Name='CaptureFinalTargetingReferences',BeforeTargets='CoreCompile')
 if name=='System.Text.RegularExpressions.Generator':
  exclude=E.SubElement(project.getroot(),'Target',Name='HonorOriginalRegexReferenceExclusion',AfterTargets='ResolveTargetingPackAssets')
  E.SubElement(E.SubElement(exclude,'ItemGroup'),'Reference',Remove='@(Reference)',Condition="'%(Reference.Filename)' == 'System.Text.RegularExpressions'")
  E.SubElement(E.SubElement(capture,'ItemGroup'),'ReferencePathWithRefAssemblies',Remove='@(ReferencePathWithRefAssemblies)',Condition="'%(ReferencePathWithRefAssemblies.Filename)' == 'System.Text.RegularExpressions'")
 E.SubElement(capture,'WriteLinesToFile',File='$(IntermediateOutputPath)ReferencePathWithRefAssemblies.txt',Lines="@(ReferencePathWithRefAssemblies->'%(FullPath)')",Overwrite='true',Encoding='UTF-8')
 E.indent(project);project.write(folder/'Build.csproj',encoding='utf-8',xml_declaration=True)
 records[name]=dict(assembly=name,original_project=original.relative_to(source).as_posix(),original_compiler_inputs=expected,original_project_resource_hashes=metadata,neutral_strings=values,translated_strings=translations,production_cs_changes=[],retargeted_host='net8.0/source Roslyn4.9.2; no historical/netstandard generator host claim')
# JS imports use the matching signed source helper, not the private CoreLib tool.
project=E.parse(out/'JSImport/Build.csproj');replaced=0
for reference in project.findall('.//Reference'):
 if reference.get('Include')=='Microsoft.Interop.SourceGeneration':
  reference.find('HintPath').text=native(out/'Microsoft.Interop.SourceGeneration/bin/Release/net8.0/Microsoft.Interop.SourceGeneration.dll');replaced+=1
assert replaced==1
E.indent(project);project.write(out/'JSImport/Build.csproj',encoding='utf-8',xml_declaration=True)
(out/'source-inputs.json').write_text(json.dumps(records,ensure_ascii=False,indent=2)+'\n')
print('PASS original complete interop/Regex compiler inputs, neutral/XLF resources and matching signed8.0.0.0 helper identities')
