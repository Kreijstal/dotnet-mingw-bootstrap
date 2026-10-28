"""Prepare the exact Roslyn4.14 compiler libraries and their original generators."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as E

root, out = (Path(p).resolve() for p in sys.argv[1:3])
resume = len(sys.argv) == 4 and sys.argv[3] == '--resume'
assert out.exists() if resume else not out.exists(), out
out.mkdir(parents=True, exist_ok=resume)
here = Path(__file__).resolve().parent
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
archive = root / 'roslyn-4.14.0-8edf7bcd.tar.gz'
assert sha(archive) == '8cf2d365d1fad52b480f7c68b5c477cb586a935ada0eb309b0c40dacfec5fe9c'
commit = '8edf7bcd4f1594c3d68a6a567469f41dbd33dd1b'
source = out / 'roslyn-source';source.mkdir(exist_ok=resume)
removed = []
if resume:
    previous = json.loads((out / 'source-inputs.json').read_text())
    assert previous['roslyn_commit'] == commit and previous['source_archive_sha256'] == sha(archive)
    for entry in previous['projects'].values():
        for path, expected in entry['original_cs'].items():
            assert sha(source / path) == expected, path
    removed = previous['removed_binary_inputs']
with tarfile.open(archive) as tar:
    assert tar.pax_headers['comment'] == commit
    for member in ([] if resume else tar):
        if not member.isfile():continue
        name = member.name.partition('/')[2]
        assert '..' not in Path(name).parts and not Path(name).is_absolute()
        data = tar.extractfile(member).read()
        if Path(name).suffix.lower() in {'.dll','.exe','.pdb','.nupkg','.res','.resources','.obj','.lib','.zip'} or data[:2] == b'MZ':
            removed.append(dict(path=name,sha256=hashlib.sha256(data).hexdigest()));continue
        path=source/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
native_root = subprocess.check_output(['cygpath','-am',str(root)],text=True).strip()
def native(path):return native_root+'/'+Path(path).resolve().relative_to(root).as_posix()
def posix(path):
    path=path.replace('\\','/');assert path.lower().startswith(native_root.lower()+'/')
    return root/path[len(native_root)+1:]
compiler = root/'cdac-bootstrap5/compiler'
key = bytes.fromhex((here/'inputs/MicrosoftShared.pubkey.hex').read_text())
assert hashlib.sha1(key).digest()[-8:][::-1].hex()=='31bf3856ad364e35'
(out/'Roslyn.public.snk').write_bytes(key)
records,localizations={},[]
def project(folder,name,exe=False):
    folder.mkdir(parents=True, exist_ok=resume)
    tree=E.Element('Project',Sdk='Microsoft.NET.Sdk');pg=E.SubElement(tree,'PropertyGroup')
    values=dict(TargetFramework='net8.0',AssemblyName=name,RootNamespace=name,OutputType='Exe' if exe else 'Library',
                UseAppHost='false',EnableDefaultItems='false',EnableNETAnalyzers='false',NuGetAudit='false',
                AllowUnsafeBlocks='true',Nullable='enable',LangVersion='preview',UseSharedCompilation='false',
                CscToolPath=native(compiler),CscToolExe='csc50.cmd',Version='4.14.0',AssemblyVersion='4.14.0.0',
                FileVersion='4.14.0.0',InformationalVersion='4.14.0+'+commit,IncludeSourceRevisionInInformationalVersion='false')
    for k,v in values.items():E.SubElement(pg,k).text=v
    return tree,pg,E.SubElement(tree,'ItemGroup')
def save(tree,path):
    E.indent(tree);E.ElementTree(tree).write(path,encoding='utf-8',xml_declaration=True)
tools=source/'src/Tools/Source/CompilerGeneratorTools/Source'
for name in ['CSharpSyntaxGenerator','BoundTreeGenerator','CSharpErrorFactsGenerator']:
    folder=out/name;tree,pg,items=project(folder,name,True)
    selected=[p for p in sorted((tools/name).rglob('*.cs')) if p.name not in {'SourceGenerator.cs','CachingSourceGenerator.cs'}]
    if name=='CSharpSyntaxGenerator':
        selected += [source/'src/Compilers'/p for p in ['CSharp/Portable/Syntax/SyntaxKind.cs','CSharp/Portable/Syntax/SyntaxKindFacts.cs','CSharp/Portable/Declarations/DeclarationModifiers.cs','Core/Portable/Symbols/WellKnownMemberNames.cs']]
    for path in selected:E.SubElement(items,'Compile',Include=native(path))
    save(tree,folder/'Build.csproj')
    records[name]=dict(original_cs={p.relative_to(source).as_posix():sha(p) for p in selected},production_cs_changes=0)

wrappers={}
for name,language in [('Microsoft.CodeAnalysis','Core'),('Microsoft.CodeAnalysis.CSharp','CSharp')]:
    main=source/'src/Compilers'/language/'Portable';original=main/(name+'.csproj')
    folder=out/'evaluation'/name;folder.mkdir(parents=True,exist_ok=resume)
    body=original.read_text(encoding='utf-8-sig').replace('$(MSBuildThisFileDirectory)',native(main)+'/')
    upstream=E.ElementTree(E.fromstring(body));upstream.getroot().attrib.pop('Sdk')
    for item in upstream.iter():
        for attribute in ['Include','Remove','Update','Project']:
            if item.tag not in {'Compile','Import','ProjectReference','EmbeddedResource'} or attribute not in item.attrib:continue
            value=item.get(attribute).replace('\\','/')
            if '$(' not in value and not value.startswith('/') and not (len(value)>1 and value[1]==':'):
                item.set(attribute,native(main/value))
    save(upstream.getroot(),folder/'upstream.proj')
    wrapper=E.Element('Project',Sdk='Microsoft.NET.Sdk');pg=E.SubElement(wrapper,'PropertyGroup')
    for k,v in dict(TargetFramework='net8.0',NetRoslynSourceBuild='net8.0;net9.0',ImportDirectoryBuildProps='false',
                    ImportDirectoryBuildTargets='false',EnableDefaultItems='false',DotNetBuildSourceOnly='true',
                    RootNamespace=name,DefineConstants='$(DefineConstants);DOTNET_BUILD_FROM_SOURCE',
                    CompilerGeneratedFilesOutputPath=native(main/'Generated')).items():E.SubElement(pg,k).text=v
    items=E.SubElement(wrapper,'ItemGroup')
    for path in sorted(main.rglob('*.cs')):E.SubElement(items,'Compile',Include=native(path))
    for path in sorted(main.rglob('*.resx')):E.SubElement(items,'EmbeddedResource',Include=native(path))
    E.SubElement(wrapper,'Import',Project='upstream.proj')
    save(wrapper,folder/(name+'.csproj'));wrappers[name]=native(folder/(name+'.csproj'))
(out/'wrappers.json').write_text(json.dumps(wrappers,indent=2)+'\n')
tool=out/'evaluation-tool';tree,pg,items=project(tool,'EvaluateRoslynProjects',True)
E.SubElement(items,'Compile',Include=native(here/'inputs/EvaluateRoslynProjects.cs'))
sdk=Path('/clang64/lib/dotnet/sdk/8.0.131')
sdk_native=subprocess.check_output(['cygpath','-am',str(sdk)],text=True).strip()
for name in ['Microsoft.Build','Microsoft.Build.Framework','Microsoft.Build.Utilities.Core','Microsoft.NET.StringTools']:
    r=E.SubElement(items,'Reference',Include=name);E.SubElement(r,'HintPath').text=sdk_native+'/'+name+'.dll'
save(tree,tool/'Build.csproj')
feed=root/'roslyn-deps9-1/feed'
(out/'NuGet.config').write_text('<configuration><packageSources><clear/><add key="source-dependencies9" value="'+native(feed)+'"/><add key="source-private" value="'+native(out/'feed')+'"/></packageSources></configuration>\n')
dotnet='/clang64/lib/dotnet/dotnet.exe'
env=dict(os.environ,MSBuildEnableWorkloadResolver='false')
with (out/'evaluation-build.log').open('w') as log:
    subprocess.run([dotnet,'build',native(tool/'Build.csproj'),'-c','Release','--configfile',native(out/'NuGet.config'),'-p:UseSharedCompilation=false','-v:minimal'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
env.update(MSBuildSDKsPath=sdk_native+'/Sdks',MSBUILD_EXE_PATH=sdk_native+'/MSBuild.dll')
with (out/'evaluation.log').open('w') as log:
    subprocess.run([dotnet,native(tool/'bin/Release/net8.0/EvaluateRoslynProjects.dll'),native(out/'wrappers.json'),native(out/'evaluated.json')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
evaluated=json.loads((out/'evaluated.json').read_text())
settings=E.parse(source/'eng/targets/Settings.props')
def key_value(name):
    value=settings.findtext('.//'+name)
    while value and value.startswith('$(') and value.endswith(')'):value=settings.findtext('.//'+value[2:-1])
    assert value and all(c in '0123456789abcdefABCDEF' for c in value),(name,value)
    return value
for name,language in [('Microsoft.CodeAnalysis','Core'),('Microsoft.CodeAnalysis.CSharp','CSharp')]:
    folder=out/name;tree,pg,items=project(folder,name)
    for k,v in dict(SignAssembly='true',PublicSign='true',AssemblyOriginatorKeyFile='../Roslyn.public.snk',
                    PackageId='Microsoft.CodeAnalysis.Common' if language=='Core' else name,PackageLicenseExpression='MIT',
                    RepositoryUrl='https://github.com/dotnet/roslyn',RepositoryCommit=commit,
                    DefineConstants=evaluated[name]['properties']['DefineConstants']+';DOTNET_BUILD_FROM_SOURCE',NoWarn='CS0436;CS0618;SYSLIB0051;SYSLIB0050;RSEXPERIMENTAL005').items():E.SubElement(pg,k).text=v
    selected=[posix(item['full_path']) for item in evaluated[name]['items']['Compile']]
    if language=='CSharp':selected=[p for p in selected if not p.is_relative_to(source/'src/Compilers/CSharp/Portable/Generated')]
    selected=list(dict.fromkeys(selected));assert all(p.is_file() for p in selected)
    for path in selected:E.SubElement(items,'Compile',Include=native(path))
    if language=='CSharp':E.SubElement(items,'Compile',Include=native(out/'generated/*.cs'))
    for dependency in ['System.Collections.Immutable','System.Reflection.Metadata']:
        E.SubElement(items,'PackageReference',Include=dependency,Version='[9.0.0]')
    if language=='CSharp':E.SubElement(items,'PackageReference',Include='Microsoft.CodeAnalysis.Common',Version='[4.14.0]')
    resources={}
    for item in evaluated[name]['items']['EmbeddedResource']:
        path=posix(item['full_path']);resources[path]=item['metadata']
    for path,metadata in resources.items():
        if path.suffix!='.resx':
            relative=path.relative_to(source/'src/Compilers'/language/'Portable').as_posix().replace('/','.')
            E.SubElement(items,'EmbeddedResource',Include=native(path),LogicalName=name+'.'+relative);continue
        namespace=metadata.get('Namespace') or name
        classname=metadata.get('ClassName') or path.stem
        if '.' in classname:namespace,_,classname=classname.rpartition('.')
        logical=(metadata.get('LogicalName') or metadata.get('ManifestResourceName') or namespace+'.'+path.stem).removesuffix('.resources')
        code=['#nullable enable','namespace '+namespace+' { internal static class '+classname+' {',
              'internal static global::System.Resources.ResourceManager ResourceManager { get; } = new global::System.Resources.ResourceManager('+json.dumps(logical)+', typeof('+classname+').Assembly);',
              'internal static global::System.Globalization.CultureInfo? Culture { get; set; }']
        for item in E.parse(path).iter('data'):
            key_name=item.get('name');assert key_name.isidentifier(),key_name
            code.append('internal static string @'+key_name+' => ResourceManager.GetString('+json.dumps(key_name)+', Culture)!;')
        code.append('}}');generated=folder/(namespace+'.'+classname+'.Generated.cs');generated.write_text('\n'.join(code)+'\n')
        E.SubElement(items,'Compile',Include=generated.name);E.SubElement(items,'EmbeddedResource',Include=native(path),LogicalName=logical+'.resources')
        localizations.append(dict(assembly=name,logical_name=logical,culture='',strings={i.get('name'):i.findtext('value') or '' for i in E.parse(path).iter('data')}))
        ns={'x':'urn:oasis:names:tc:xliff:document:1.2'}
        for translation in sorted((path.parent/'xlf').glob(path.stem+'.*.xlf')):
            xlf=E.parse(translation);culture=xlf.find('x:file',ns).get('target-language')
            values={u.get('id'):''.join(u.find('x:target',ns).itertext()) for u in xlf.findall('.//x:trans-unit',ns) if u.find('x:target',ns) is not None}
            localized=E.parse(path)
            for i in localized.iter('data'):
                if i.get('name') in values:i.find('value').text=values[i.get('name')]
            target=folder/(logical+'.'+culture+'.resx');localized.write(target,encoding='utf-8',xml_declaration=True)
            E.SubElement(items,'EmbeddedResource',Include=target.name,LogicalName=logical+'.'+culture+'.resources',Culture=culture,WithCulture='true')
            localizations.append(dict(assembly=name,logical_name=logical,culture=culture,strings={i.get('name'):i.findtext('value') or '' for i in localized.iter('data')}))
    friends=[]
    original_friends={item.get('Include'):item.get('Key','') for item in E.parse(source/'src/Compilers'/language/'Portable'/(name+'.csproj')).iter('InternalsVisibleTo')}
    for item in evaluated[name]['items']['InternalsVisibleTo']:
        value=original_friends.get(item['identity'],item['metadata'].get('Key',''))
        public=key_value(value[2:-1]) if value.startswith('$(') else value or key.hex()
        friends.append('[assembly: System.Runtime.CompilerServices.InternalsVisibleTo('+json.dumps(item['identity']+', PublicKey='+public)+')]')
    if language=='CSharp':friends.append('[assembly: Microsoft.CodeAnalysis.CommitHash('+json.dumps(commit)+')]')
    (folder/'Friends.Generated.cs').write_text('\n'.join(dict.fromkeys(friends))+'\n');E.SubElement(items,'Compile',Include='Friends.Generated.cs')
    save(tree,folder/'Build.csproj')
    records[name]=dict(original_project='src/Compilers/'+language+'/Portable/'+name+'.csproj',original_project_sha256=sha(source/'src/Compilers'/language/'Portable'/(name+'.csproj')),
                       original_cs={p.relative_to(source).as_posix():sha(p) for p in selected},production_cs_changes=0,
                       original_resources={p.relative_to(source).as_posix():sha(p) for p in resources},friend_declarations=len(friends),
                       original_declared_dependencies={'System.Collections.Immutable':'9.0.0','System.Reflection.Metadata':'9.0.0'})
(out/'source-inputs.json').write_text(json.dumps(dict(roslyn_version='4.14.0',roslyn_commit=commit,source_archive_sha256=sha(archive),removed_binary_inputs=removed,projects=records,full_dependency_goal_complete=False),indent=2)+'\n')
(out/'localization.json').write_text(json.dumps(localizations,ensure_ascii=False,indent=2)+'\n')
print('Prepared original Roslyn4.14 source projects:',{name:len(r['original_cs']) for name,r in records.items()},'localized payloads',len(localizations))
