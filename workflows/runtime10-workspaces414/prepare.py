"""Prepare complete original Workspaces4.14 and source9 Channels dependency."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import xml.etree.ElementTree as E
import zipfile

root, out = (Path(p).resolve() for p in sys.argv[1:])
assert not out.exists(), out
out.mkdir(parents=True)
here = Path(__file__).resolve().parent
roslyn = root / 'roslyn414-1/roslyn-source'
runtime = root / 'roslyn-deps9-1/runtime-source'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert json.loads((root/'roslyn414-1/source-inputs.json').read_text())['roslyn_commit']=='8edf7bcd4f1594c3d68a6a567469f41dbd33dd1b'
assert json.loads((root/'roslyn-deps9-1/source-inputs.json').read_text())['runtime_commit']=='9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3'
native_root=subprocess.check_output(['cygpath','-am',str(root)],text=True).strip()
def native(path):return native_root+'/'+Path(path).resolve().relative_to(root).as_posix()
def posix(path):
    path=path.replace('\\','/');assert path.lower().startswith(native_root.lower()+'/')
    return root/path[len(native_root)+1:]
def save(tree,path):
    E.indent(tree);E.ElementTree(tree).write(path,encoding='utf-8',xml_declaration=True)
names={
 'System.Threading.Channels':runtime/'src/libraries/System.Threading.Channels/src/System.Threading.Channels.csproj',
 'Microsoft.CodeAnalysis.Workspaces':roslyn/'src/Workspaces/Core/Portable/Microsoft.CodeAnalysis.Workspaces.csproj',
 'Microsoft.CodeAnalysis.CSharp.Workspaces':roslyn/'src/Workspaces/CSharp/Portable/Microsoft.CodeAnalysis.CSharp.Workspaces.csproj'}
wrappers={}
for name,original in names.items():
    folder=out/'evaluation'/name;folder.mkdir(parents=True)
    body=original.read_text(encoding='utf-8-sig').replace('$(MSBuildThisFileDirectory)',native(original.parent)+'/')
    tree=E.ElementTree(E.fromstring(body));tree.getroot().attrib.pop('Sdk')
    for item in tree.iter():
        for attribute in ['Include','Remove','Update','Project']:
            if item.tag not in {'Compile','Import','ProjectReference','EmbeddedResource'} or attribute not in item.attrib:continue
            value=item.get(attribute).replace('\\','/')
            if '$(' not in value and not value.startswith('/') and not (len(value)>1 and value[1]==':'):
                item.set(attribute,native(original.parent/value))
    save(tree.getroot(),folder/'upstream.proj')
    wrapper=E.Element('Project',Sdk='Microsoft.NET.Sdk');pg=E.SubElement(wrapper,'PropertyGroup')
    values=dict(TargetFramework='net8.0',ImportDirectoryBuildProps='false',ImportDirectoryBuildTargets='false',
                EnableDefaultItems='false',DotNetBuildSourceOnly='true',NetRoslynSourceBuild='net8.0;net9.0',
                DefineConstants='$(DefineConstants);DOTNET_BUILD_FROM_SOURCE')
    if name=='System.Threading.Channels':
        libs=runtime/'src/libraries'
        values.update(NetCoreAppCurrent='net9.0',NetCoreAppPrevious='net8.0',NetCoreAppMinimum='net8.0',NetFrameworkMinimum='net462',
                      CommonPath=native(libs/'Common/src')+'/',CoreLibSharedDir=native(libs/'System.Private.CoreLib/src')+'/',
                      LibrariesProjectRoot=native(libs)+'/',TargetOS='windows',TargetsWindows='true')
    for k,v in values.items():E.SubElement(pg,k).text=v
    if name!='System.Threading.Channels':
        items=E.SubElement(wrapper,'ItemGroup')
        for path in sorted(original.parent.rglob('*.cs')):E.SubElement(items,'Compile',Include=native(path))
        for path in sorted(original.parent.rglob('*.resx')):E.SubElement(items,'EmbeddedResource',Include=native(path))
    E.SubElement(wrapper,'Import',Project='upstream.proj')
    path=folder/(name+'.csproj');save(wrapper,path);wrappers[name]=native(path)
(out/'wrappers.json').write_text(json.dumps(wrappers,indent=2)+'\n')
sdk_native=subprocess.check_output(['cygpath','-am','/clang64/lib/dotnet/sdk/8.0.131'],text=True).strip()
env=dict(os.environ,MSBuildSDKsPath=sdk_native+'/Sdks',MSBUILD_EXE_PATH=sdk_native+'/MSBuild.dll',MSBuildEnableWorkloadResolver='false')
tool=root/'roslyn414-1/evaluation-tool/bin/Release/net8.0/EvaluateRoslynProjects.dll'
dotnet='/clang64/lib/dotnet/dotnet.exe'
with (out/'evaluation.log').open('w') as log:
    subprocess.run([dotnet,native(tool),native(out/'wrappers.json'),native(out/'evaluated.json')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
evaluated=json.loads((out/'evaluated.json').read_text())
for key in ['MicrosoftShared','Open']:(out/(key+'.pubkey')).write_bytes(bytes.fromhex((here/'inputs'/(key+'.pubkey.hex')).read_text()))
settings=E.parse(roslyn/'eng/targets/Settings.props')
def public_key(value):
    if not value:return (out/'MicrosoftShared.pubkey').read_bytes().hex()
    while value.startswith('$(') and value.endswith(')'):
        value=settings.findtext('.//'+value[2:-1]);assert value
    assert all(c in '0123456789abcdefABCDEF' for c in value),value
    return value
localizations=[];records={}
def resource(items,folder,path,metadata,namespace,source_root):
    if path.suffix!='.resx':
        logical=metadata.get('LogicalName') or namespace+'.'+path.name
        E.SubElement(items,'EmbeddedResource',Include=native(path),LogicalName=logical);return
    classname=metadata.get('ClassName') or path.stem
    namespace=metadata.get('Namespace') or namespace
    if '.' in classname:namespace,_,classname=classname.rpartition('.')
    logical=(metadata.get('LogicalName') or metadata.get('ManifestResourceName') or namespace+'.'+path.stem).removesuffix('.resources')
    code=['#nullable enable','namespace '+namespace+' { internal static class '+classname+' {',
          'internal static global::System.Resources.ResourceManager ResourceManager { get; } = new global::System.Resources.ResourceManager('+json.dumps(logical)+', typeof('+classname+').Assembly);',
          'internal static global::System.Globalization.CultureInfo? Culture {get;set;}']
    for item in E.parse(path).iter('data'):
        key=item.get('name');assert key.isidentifier(),key
        code.append('internal static string @'+key+' => ResourceManager.GetString('+json.dumps(key)+', Culture)!;')
    code.append('}}');generated=folder/(namespace+'.'+classname+'.Generated.cs');generated.write_text('\n'.join(code)+'\n')
    E.SubElement(items,'Compile',Include=generated.name);E.SubElement(items,'EmbeddedResource',Include=native(path),LogicalName=logical+'.resources')
    localizations.append(dict(assembly=folder.name,resource=logical,culture='',strings={e.get('name'):e.findtext('value') or '' for e in E.parse(path).iter('data')}))
    ns={'x':'urn:oasis:names:tc:xliff:document:1.2'}
    for xlf_path in sorted((path.parent/'xlf').glob(path.stem+'.*.xlf')):
        xlf=E.parse(xlf_path);culture=xlf.find('x:file',ns).get('target-language')
        values={u.get('id'):''.join(u.find('x:target',ns).itertext()) for u in xlf.findall('.//x:trans-unit',ns) if u.find('x:target',ns) is not None}
        localized=E.parse(path)
        for e in localized.iter('data'):
            if e.get('name') in values:e.find('value').text=values[e.get('name')]
        target=folder/(logical+'.'+culture+'.resx');localized.write(target,encoding='utf-8',xml_declaration=True)
        E.SubElement(items,'EmbeddedResource',Include=target.name,LogicalName=logical+'.'+culture+'.resources',Culture=culture,WithCulture='true')
        localizations.append(dict(assembly=folder.name,resource=logical,culture=culture,strings={e.get('name'):e.findtext('value') or '' for e in localized.iter('data')}))

dependencies={
 'System.Threading.Channels':{},
 'Microsoft.CodeAnalysis.Workspaces':{'Microsoft.CodeAnalysis.Common':'4.14.0','Humanizer.Core':'2.14.1','System.Composition':'9.0.0','System.IO.Pipelines':'9.0.0','System.Threading.Channels':'9.0.0'},
 'Microsoft.CodeAnalysis.CSharp.Workspaces':{'Microsoft.CodeAnalysis.Common':'4.14.0','Microsoft.CodeAnalysis.CSharp':'4.14.0','Microsoft.CodeAnalysis.Workspaces.Common':'4.14.0','Humanizer.Core':'2.14.1'}}
for name,original in names.items():
    folder=out/name;folder.mkdir()
    is_channel=name=='System.Threading.Channels';version='9.0.0' if is_channel else '4.14.0';commit='9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' if is_channel else '8edf7bcd4f1594c3d68a6a567469f41dbd33dd1b'
    source_root=runtime if is_channel else roslyn
    tree=E.Element('Project',Sdk='Microsoft.NET.Sdk');pg=E.SubElement(tree,'PropertyGroup')
    namespace=evaluated[name]['properties']['RootNamespace'] or name
    values=dict(TargetFramework='net8.0',AssemblyName=name,RootNamespace=namespace,PackageId='Microsoft.CodeAnalysis.Workspaces.Common' if name=='Microsoft.CodeAnalysis.Workspaces' else name,
                Version=version,AssemblyVersion=version+'.0',FileVersion=version+'.0',InformationalVersion=version+'+'+commit,
                IncludeSourceRevisionInInformationalVersion='false',EnableDefaultItems='false',EnableNETAnalyzers='false',
                NuGetAudit='false',Nullable='enable',LangVersion='preview',AllowUnsafeBlocks='true',UseSharedCompilation='false',
                CscToolPath=native(root/'cdac-bootstrap5/compiler'),CscToolExe='csc50.cmd',SignAssembly='true',PublicSign='true',
                AssemblyOriginatorKeyFile='../'+('Open' if is_channel else 'MicrosoftShared')+'.pubkey',
                DefineConstants=evaluated[name]['properties']['DefineConstants'],NoWarn='CS0436;CS0618;SYSLIB0051;SYSLIB0050;RSEXPERIMENTAL005',
                PackageLicenseExpression='MIT',RepositoryUrl='https://github.com/dotnet/'+('runtime' if is_channel else 'roslyn'),RepositoryCommit=commit)
    for k,v in values.items():E.SubElement(pg,k).text=v
    items=E.SubElement(tree,'ItemGroup')
    selected=list(dict.fromkeys(posix(i['full_path']) for i in evaluated[name]['items']['Compile']))
    assert selected and all(p.is_file() for p in selected)
    if not is_channel:assert not any('/Storage/SQLite/' in str(p) for p in selected)
    resources={posix(i['full_path']):i['metadata'] for i in evaluated[name]['items']['EmbeddedResource']}
    if is_channel:
        selected.append(runtime/'src/libraries/Common/src/System/SR.cs')
        res=original.parent/'Resources/Strings.resx'
        logical='FxResources.'+name+'.SR'
        E.SubElement(items,'EmbeddedResource',Include=native(res),LogicalName=logical+'.resources')
        code=['namespace System { internal static partial class SR {',
              'internal static global::System.Resources.ResourceManager ResourceManager { get; } = new global::System.Resources.ResourceManager("'+logical+'", typeof(SR).Assembly);']
        for e in E.parse(res).iter('data'):code.append('internal static string @'+e.get('name')+' => GetResourceString('+json.dumps(e.get('name'))+');')
        code.append('}}');(folder/'SR.Generated.cs').write_text('\n'.join(code)+'\n');E.SubElement(items,'Compile',Include='SR.Generated.cs')
        resources[res]={}
        target=E.SubElement(tree,'Target',Name='RemoveFrameworkSelfReference',BeforeTargets='CoreCompile');group=E.SubElement(target,'ItemGroup')
        for kind in ['ReferencePath','ReferencePathWithRefAssemblies']:
            E.SubElement(group,kind,Remove='@('+kind+')',Condition="'%({0}.FrameworkReferenceName)' == 'Microsoft.NETCore.App' And '%({0}.Filename)' == '$(AssemblyName)'".format(kind))
    else:
        for path,metadata in resources.items():resource(items,folder,path,metadata,namespace,source_root)
    for path in selected:E.SubElement(items,'Compile',Include=native(path))
    for dep,dep_version in dependencies[name].items():
        kwargs={'PrivateAssets':'compile'} if dep=='Humanizer.Core' else {}
        E.SubElement(items,'PackageReference',Include=dep,Version='['+dep_version+']',**kwargs)
    original_xml=E.parse(original);friends=[]
    for kind in ['InternalsVisibleTo','RestrictedInternalsVisibleTo']:
        raw={e.get('Include'):e.attrib for e in original_xml.iter(kind)}
        for item in evaluated[name]['items'][kind]:
            metadata=raw.get(item['identity'],item['metadata']);key=public_key(metadata.get('Key',''))
            friends.append('[assembly: System.Runtime.CompilerServices.InternalsVisibleTo('+json.dumps(item['identity']+', PublicKey='+key)+')]')
            if kind=='RestrictedInternalsVisibleTo':
                ns=metadata.get('Namespace') or 'Microsoft.CodeAnalysis.ExternalAccess.'+metadata['Partner']+'.Api'
                friends.append('[assembly: System.Runtime.CompilerServices.RestrictedInternalsVisibleTo('+json.dumps(item['identity'])+', '+json.dumps(ns)+')]')
    if friends:
        (folder/'Friends.Generated.cs').write_text('\n'.join(dict.fromkeys(friends))+'\n');E.SubElement(items,'Compile',Include='Friends.Generated.cs')
    # Shipping license text stays with every private package as well.
    for path,target_name in [(source_root/('LICENSE.TXT' if is_channel else 'License.txt'),'LICENSE'),(source_root/('THIRD-PARTY-NOTICES.TXT' if is_channel else 'THIRD-PARTY-NOTICES.txt'),'THIRD-PARTY-NOTICES')]:
        if path.is_file():E.SubElement(items,'None',Include=native(path),Pack='true',PackagePath=target_name)
    save(tree,folder/'Build.csproj')
    resource_hashes={p.relative_to(source_root).as_posix():sha(p) for path in resources for p in [path,*sorted((path.parent/'xlf').glob(path.stem+'.*.xlf'))]}
    records[name]=dict(source_repository='runtime' if is_channel else 'roslyn',original_project=original.relative_to(source_root).as_posix(),
                       original_project_sha256=sha(original),original_cs={p.relative_to(source_root).as_posix():sha(p) for p in selected},
                       original_resources=resource_hashes,production_cs_changes=0,original_supported_framework='net8.0',
                       dependencies=dependencies[name],friend_declarations=len(friends),original_sqlite_source_build_exclusion=not is_channel)

feed=out/'feed';feed.mkdir()
meta=runtime/'src/libraries/System.Composition/src/System.Composition.csproj'
meta_deps=[Path(e.get('Include').replace('\\','/')).stem for e in E.parse(meta).iter('ProjectReference')]
assert set(meta_deps)=={'System.Composition.'+s for s in ['AttributedModel','Convention','Hosting','Runtime','TypedParts']}
spec=E.Element('package');md=E.SubElement(spec,'metadata')
for k,v in {'id':'System.Composition','version':'9.0.0','authors':'source runtime9 build','description':'Original lightweight MEF composition metapackage, private source net8 build'}.items():E.SubElement(md,k).text=v
E.SubElement(md,'license',type='expression').text='MIT'
E.SubElement(md,'repository',type='git',url='https://github.com/dotnet/runtime',commit='9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3')
group=E.SubElement(E.SubElement(md,'dependencies'),'group',targetFramework='net8.0')
for dep in meta_deps:E.SubElement(group,'dependency',id=dep,version='[9.0.0]')
with zipfile.ZipFile(feed/'System.Composition.9.0.0.nupkg','w',zipfile.ZIP_DEFLATED) as z:
    z.writestr('System.Composition.nuspec',E.tostring(spec,encoding='utf-8',xml_declaration=True));z.writestr('lib/net8.0/_._','')
    for path in ['LICENSE.TXT','THIRD-PARTY-NOTICES.TXT']:z.write(runtime/path,path)
    z.writestr('source-provenance.json',json.dumps(dict(original_project=meta.relative_to(runtime).as_posix(),original_project_sha256=sha(meta),dependencies=meta_deps,source_runtime_commit='9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3'),indent=2)+'\n')
(out/'source-inputs.json').write_text(json.dumps(dict(roslyn_version='4.14.0',roslyn_commit='8edf7bcd4f1594c3d68a6a567469f41dbd33dd1b',runtime_version='9.0.0',runtime_commit='9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3',
    projects=records,composition_meta_original_project_sha256=sha(meta),original_channels_minimum='7.0.0',resolved_source_channels_version='9.0.0',
    channels_resolution_satisfies_original_minimum=True,full_dependency_goal_complete=False),indent=2)+'\n')
(out/'localization.json').write_text(json.dumps(localizations,ensure_ascii=False,indent=2)+'\n')
installed=subprocess.check_output(['cygpath','-am','/clang64/lib/dotnet/library-packs'],text=True).strip()
cfg=E.Element('configuration');sources=E.SubElement(cfg,'packageSources');E.SubElement(sources,'clear')
for label,path in [('private',native(feed)),('source-compiler414',native(root/'roslyn414-1/feed')),('source-runtime9',native(root/'roslyn-deps9-1/feed')),('source-installed',installed)]:E.SubElement(sources,'add',key=label,value=path)
save(cfg,out/'NuGet.config')
print('Prepared original Workspaces4.14/Channels9:',{n:len(p['original_cs']) for n,p in records.items()},'resource sets',len(localizations))
