"""Fresh source-feed consumer: real Workspaces/Channels behavior and full API/resources."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import xml.etree.ElementTree as E
import zipfile

root,out,check=(Path(p).resolve() for p in sys.argv[1:])
assert not check.exists(),check
check.mkdir(parents=True)
here=Path(__file__).resolve().parent
native_root=subprocess.check_output(['cygpath','-am',str(root)],text=True).strip()
def native(path):return native_root+'/'+Path(path).resolve().relative_to(root).as_posix()
def posix(path):
    path=path.replace('\\','/')
    if path.lower().startswith(native_root.lower()+'/'):return root/path[len(native_root)+1:]
    return Path(subprocess.check_output(['cygpath','-u',path],text=True).strip())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=json.loads((out/'source-inputs.json').read_text())
roslyn=root/'roslyn414-1/roslyn-source';runtime=root/'roslyn-deps9-1/runtime-source'
settings=E.parse(roslyn/'eng/targets/Settings.props')
def key(value):
    if not value:value=settings.findtext('.//MicrosoftSharedPublicKey')
    while value.startswith('$('):value=settings.findtext('.//'+value[2:-1])
    return value
expected={}
for name,record in source['projects'].items():
    source_root=runtime if record['source_repository']=='runtime' else roslyn
    for path,digest in {**record['original_cs'],**record['original_resources']}.items():assert sha(source_root/path)==digest,(name,path)
    if record['source_repository']=='runtime':continue
    original=E.parse(roslyn/record['original_project']);friends=[]
    for kind in ['InternalsVisibleTo','RestrictedInternalsVisibleTo']:
        for item in original.iter(kind):
            friends.append('[assembly: System.Runtime.CompilerServices.InternalsVisibleTo('+json.dumps(item.get('Include')+', PublicKey='+key(item.get('Key','')))+')]')
            if kind=='RestrictedInternalsVisibleTo':
                ns=item.get('Namespace') or 'Microsoft.CodeAnalysis.ExternalAccess.'+item.get('Partner')+'.Api'
                friends.append('[assembly: System.Runtime.CompilerServices.RestrictedInternalsVisibleTo('+json.dumps(item.get('Include'))+', '+json.dumps(ns)+')]')
    expected[name]=dict(friend_attributes=list(dict.fromkeys(friends)))
    suffix='common' if name=='Microsoft.CodeAnalysis.Workspaces' else 'csharp'
    folder=(roslyn/record['original_project']).parent
    (check/('api-'+suffix+'.txt')).write_text((folder/'PublicAPI.Shipped.txt').read_text().rstrip()+'\n'+(folder/'PublicAPI.Unshipped.txt').read_text().rstrip()+'\n')
(check/'source-items.json').write_text(json.dumps(expected,indent=2)+'\n')
tree=E.Element('Project',Sdk='Microsoft.NET.Sdk');pg=E.SubElement(tree,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',AssemblyName='Check',OutputType='Exe',UseAppHost='false',EnableDefaultItems='false',
                EnableNETAnalyzers='false',NuGetAudit='false',LangVersion='preview',Nullable='disable',
                CscToolPath=native(root/'cdac-bootstrap5/compiler'),CscToolExe='csc50.cmd').items():E.SubElement(pg,k).text=v
items=E.SubElement(tree,'ItemGroup')
for name in ['Check.cs','CheckApi.cs','ChannelChecks.cs']:E.SubElement(items,'Compile',Include=native(here/'inputs'/name))
E.SubElement(items,'PackageReference',Include='Microsoft.CodeAnalysis.CSharp.Workspaces',Version='[4.14.0]')
E.ElementTree(tree).write(check/'Check.csproj',encoding='utf-8',xml_declaration=True)
env=dict(os.environ,DOTNET_CLI_HOME=native(check/'cli-home'),NUGET_PACKAGES=native(check/'cache'),DOTNET_CLI_TELEMETRY_OPTOUT='1',MSBuildEnableWorkloadResolver='false')
dotnet='/clang64/lib/dotnet/dotnet.exe'
with (check/'build.log').open('w') as log:
    subprocess.run([dotnet,'build',native(check/'Check.csproj'),'-c','Release','--configfile',native(out/'NuGet.config'),'-p:UseSharedCompilation=false','-v:minimal'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
feeds=[out/'feed',root/'roslyn414-1/feed',root/'roslyn-deps9-1/feed',Path('/clang64/lib/dotnet/library-packs')]
feed_packages={}
for feed in feeds:
    for p in feed.glob('*.nupkg'):feed_packages.setdefault(p.name.lower(),p)
assets=json.loads((check/'obj/project.assets.json').read_text());binary=check/'bin/Release/net8.0';packages={}
target=next(iter(assets['targets'].values()))
for library,entry in assets['libraries'].items():
    if entry['type']!='package':continue
    ident,version=library.split('/');name=ident.lower()+'.'+version+'.nupkg';assert name in feed_packages,library
    archive=feed_packages[name];cache=check/'cache'/entry['path']
    assert (cache/name).read_bytes()==archive.read_bytes(),library
    with zipfile.ZipFile(archive) as z:
        for path in z.namelist():
            if path.endswith('.dll'):assert (cache/path).read_bytes()==z.read(path),(library,'cache',path)
        selected={path for kind in ['runtime','resource'] for path in target[library].get(kind,{}) if path.endswith('.dll')}
        for path in selected:
            assert path.startswith('lib/net8.0/'),(library,path)
            assert (binary/path.removeprefix('lib/net8.0/')).read_bytes()==z.read(path),(library,'selected asset',path)
    packages[library]=sha(archive)
refs=subprocess.check_output(['cygpath','-am','/clang64/lib/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0'],text=True).strip()
command=[dotnet,native(binary/'Check.dll'),native(out/'localization.json'),native(check/'source-items.json'),refs,native(check)]
with (check/'run.log').open('w') as log:
    result=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
(check/'run.exit').write_text(str(result.returncode)+'\n')
assert result.returncode==0,(result.returncode,check/'run.log')
(check/'results.json').write_text(json.dumps(dict(command=command,exit_code=0,source_package_sha256=packages,
    all_fresh_cache_archives_and_net8_managed_assets_match=True,source_production_cs_changes=0,
    full_original_generator_bundle_complete=False,full_dependency_goal_complete=False),indent=2)+'\n')
print('PASS fresh source Workspaces4.14: complete APIs/friends/resources, MEF/refactoring/code actions and real source Channels9 behavior')
