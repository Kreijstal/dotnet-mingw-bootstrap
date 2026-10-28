"""Prepare complete original servicing generators and build-only metadata tools."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,xml.etree.ElementTree as E
root=Path(sys.argv[1]).resolve();source=root/'m/src/runtime-8.0.31-native';out=root/'managed-build';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip()
libs=source/'src/libraries';common=libs/'Common/src';core=libs/'System.Private.CoreLib/src';interop=libs/'System.Runtime.InteropServices/gen'
projects={'Microsoft.Interop.SourceGeneration':interop/'Microsoft.Interop.SourceGeneration/Microsoft.Interop.SourceGeneration.csproj','Microsoft.Interop.LibraryImportGenerator':interop/'LibraryImportGenerator/LibraryImportGenerator.csproj','Microsoft.Interop.ComInterfaceGenerator':interop/'ComInterfaceGenerator/ComInterfaceGenerator.csproj','System.Private.CoreLib.Generators':libs/'System.Private.CoreLib/gen/System.Private.CoreLib.Generators.csproj','System.Text.RegularExpressions.Generator':libs/'System.Text.RegularExpressions/gen/System.Text.RegularExpressions.Generator.csproj'}
only=set(sys.argv[2:]);records=json.loads((out/'source-inputs.json').read_text()) if (out/'source-inputs.json').is_file() else {}
for name,project in projects.items():
    if only and name not in only:continue
    folder=out/name;folder.mkdir();tree=E.parse(project);tree.getroot().attrib.pop('Sdk');selected=[];removed=[]
    # The interop gen Directory.Build.props enables SDK defaults; the CoreLib
    # generator lists its complete seven source inputs explicitly.
    if name!='System.Private.CoreLib.Generators':selected.extend(sorted(project.parent.rglob('*.cs')))
    for group in tree.getroot().findall('ItemGroup'):
        for item in list(group):
            if item.tag=='Compile':
                path=item.get('Include').replace('\\','/').replace('$(CommonPath)',str(common)+'/').replace('$(CoreLibSharedDir)',str(core)+'/').replace('$(LibrariesProjectRoot)',str(libs)+'/')
                file=Path(path) if Path(path).is_absolute() else project.parent/path
                assert file.is_file(),file;selected.append(file.resolve());group.remove(item)
            elif item.tag=='ProjectReference':
                original=(project.parent/item.get('Include').replace('\\','/')).resolve()
                peer=next(n for n,p in projects.items() if p==original)
                item.set('Include','../'+peer+'/Build.csproj')
            elif item.tag=='PackageReference':
                package=item.get('Include')
                if package=='Microsoft.CodeAnalysis':item.set('Include','Microsoft.CodeAnalysis.CSharp');removed.append('Microsoft.CodeAnalysis meta-package -> actual source CSharp/Common closure')
                item.set('Version','[3.3.4]' if package=='Microsoft.CodeAnalysis.Analyzers' else '[4.9.2]')
    assert len(selected)==len(set(selected)),name
    E.indent(tree);tree.write(folder/'upstream.proj',encoding='utf-8',xml_declaration=True)
    wrapper=E.Element('Project',Sdk='Microsoft.NET.Sdk');props=E.SubElement(wrapper,'PropertyGroup')
    for k,v in dict(ImportDirectoryBuildProps='false',ImportDirectoryBuildTargets='false',EnableDefaultItems='false',TargetFramework='net8.0',NetCoreAppToolCurrent='net8.0',CoreLibSharedDir=native(core)+'/',CommonPath=native(common)+'/',LibrariesProjectRoot=native(libs)+'/').items():E.SubElement(props,k).text=v
    E.SubElement(wrapper,'Import',Project='upstream.proj');props=E.SubElement(wrapper,'PropertyGroup')
    for k,v in dict(TargetFramework='net8.0',TargetFrameworks='',AssemblyName=name,EnableDefaultItems='false',EnableNETAnalyzers='false',NuGetAudit='false',Nullable='enable',CopyLocalLockFileAssemblies='true',Version='8.0.31',InformationalVersion='8.0.31+1219a42122cf5190c7f512850557e38c422430dc',IncludeSourceRevisionInInformationalVersion='false').items():E.SubElement(props,k).text=v
    items=E.SubElement(wrapper,'ItemGroup')
    for path in selected:E.SubElement(items,'Compile',Include=os.path.relpath(path,folder).replace('\\','/'))
    resource=project.parent/'Resources/Strings.resx' if name=='System.Text.RegularExpressions.Generator' else interop/'Common/Resources/Strings.resx'
    generated=[]
    if name!='System.Private.CoreLib.Generators':
        anchor='FxResources.'+name+'.SR';logical=anchor+'.resources'
        E.SubElement(items,'EmbeddedResource',Include=native(resource),LogicalName=logical)
        E.SubElement(items,'Compile',Include=native(common/'System/SR.cs'))
        code=['namespace '+anchor.rsplit('.',1)[0]+' { internal static class SR {} }','namespace System { internal static partial class SR {','internal static global::System.Resources.ResourceManager ResourceManager { get; } = new global::System.Resources.ResourceManager("'+anchor+'", typeof('+anchor+').Assembly);']
        for entry in E.parse(resource).iter('data'):
            key=entry.get('name');assert key.isidentifier(),key;code.append('internal static string '+key+' => GetResourceString("'+key+'");')
        code.append('}}');(folder/'SR.Generated.cs').write_text('\n'.join(code)+'\n');E.SubElement(items,'Compile',Include='SR.Generated.cs');generated.append('SR.Generated.cs')
    capture=E.SubElement(wrapper,'Target',Name='CaptureServicingCompilerInputs',BeforeTargets='CoreCompile')
    if name=='System.Text.RegularExpressions.Generator':
        # Honor the original DefaultReferenceExclusion item for the net8 tool host.
        E.SubElement(E.SubElement(capture,'ItemGroup'),'ReferencePath',Remove='@(ReferencePath)',Condition="'%(ReferencePath.Filename)' == 'System.Text.RegularExpressions'")
    for kind in ['Compile','ReferencePath','Analyzer']:E.SubElement(capture,'WriteLinesToFile',File='$(IntermediateOutputPath)'+kind+'.txt',Lines='@('+kind+"->'%(FullPath)')",Overwrite='true',Encoding='UTF-8')
    E.indent(wrapper);E.ElementTree(wrapper).write(folder/'Build.csproj',encoding='utf-8',xml_declaration=True)
    records[name]=dict(original_project=str(project.relative_to(source)),original_project_sha256=sha(project),original_cs={str(p.relative_to(source)):sha(p) for p in selected},generated_sources=generated,retargeted_framework='net8.0',package_remapping=removed,production_cs_changes=[])
# This is a private build-only task assembly, not a replacement ILLink.Tasks package.
tools=out/'RuntimeBuildTools';tools.mkdir(exist_ok=True);p=E.Element('Project',Sdk='Microsoft.NET.Sdk');props=E.SubElement(p,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',EnableDefaultItems='false',EnableNETAnalyzers='false',NuGetAudit='false',Nullable='disable',AssemblyName='RuntimeServicing.BuildTasks').items():E.SubElement(props,k).text=v
items=E.SubElement(p,'ItemGroup');task=source/'src/tools/illink/src/ILLink.Tasks/CreateRuntimeRootDescriptorFile.cs';E.SubElement(items,'Compile',Include=native(task))
for name in ['Microsoft.Build.Framework','Microsoft.Build.Utilities.Core']:
    ref=E.SubElement(items,'Reference',Include=name);E.SubElement(ref,'HintPath').text=native(Path('/clang64/lib/dotnet/sdk/8.0.131')/(name+'.dll'));E.SubElement(ref,'Private').text='false'
E.indent(p);E.ElementTree(p).write(tools/'Build.csproj',encoding='utf-8',xml_declaration=True)
records['RuntimeBuildTools']=dict(original_cs={str(task.relative_to(source)):sha(task)},purpose='Private build-only original runtime-root descriptor task; not an ILLink package')
(out/'NuGet.config').write_text('<configuration><packageSources><clear/><add key="installed-source" value="'+native(Path('/clang64/lib/dotnet/library-packs'))+'"/></packageSources></configuration>\n')
(out/'source-inputs.json').write_text(json.dumps(records,indent=2)+'\n')
print('PASS complete original servicing generator source selections/resources and original build-only runtime-root task:',{n:len(r['original_cs']) for n,r in records.items()})
