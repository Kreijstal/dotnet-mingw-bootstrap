"""Independently audit both source MSAL split artifacts and the original compiler inputs."""
from pathlib import Path
import base64,hashlib,io,json,re,subprocess,tarfile,xml.etree.ElementTree as E,zipfile

root=Path('mingw-w64-dotnet-msal');sha=lambda b:hashlib.sha256(b).hexdigest()
srcinfo=(root/'.SRCINFO').read_text();sources=re.findall(r'^\s+source = (.+)$',srcinfo,re.M);hashes=re.findall(r'^\s+sha256sums = (.+)$',srcinfo,re.M)
assert len(sources)==len(hashes)==17
for name,digest in zip(sources,hashes):
    with (root/name.split('::')[0]).open('rb') as f:actual=hashlib.file_digest(f,'sha256').hexdigest()
    assert digest!='SKIP' and actual==digest,name
original={}
with tarfile.open(root/sources[0].split('::')[0],'r|gz') as t:
    for member in t:
        if member.isfile():original[member.name.split('/',1)[1]]=t.extractfile(member).read()
inputs=json.loads((root/'source-inputs.json').read_text());json_inventory=json.loads(Path('tests/powershell-deps/system-text-json8-source-inventory-20261027.json').read_text())
specs=[('Microsoft.Identity.Client','msal',1068,421,{'Microsoft.IdentityModel.Abstractions':'6.35.0','System.Text.Json':'8.0.5'},
       {'mingw-w64-clang-x86_64-dotnet-runtime=8.0.131','mingw-w64-clang-x86_64-dotnet-identitymodel-abstractions=6.35.0','mingw-w64-clang-x86_64-dotnet-system-text-json8=8.0.5'}),
       ('Microsoft.Identity.Client.Extensions.Msal','msal-extensions',69,28,{'Microsoft.Identity.Client':'4.61.3','System.Security.Cryptography.ProtectedData':'8.0.0'},
       {'mingw-w64-clang-x86_64-dotnet-runtime=8.0.131','mingw-w64-clang-x86_64-dotnet-msal=4.61.3','mingw-w64-clang-x86_64-dotnet-protecteddata=8.0.31'})]
records=[];inventories=[]
for name,suffix,api_count,cs_count,dependencies,native in specs:
    artifact=root/('mingw-w64-clang-x86_64-dotnet-'+suffix+'-4.61.3-1-any.pkg.tar.zst')
    process=subprocess.Popen(['zstd','-dc',str(artifact)],stdout=subprocess.PIPE)
    with tarfile.open(fileobj=process.stdout,mode='r|') as t:payload={m.name.removeprefix('./'):t.extractfile(m).read() for m in t if m.isfile()}
    while process.stdout.read(1024*1024):pass
    assert process.wait()==0
    assert 'pkgver = 4.61.3-1\n' in payload['.PKGINFO'].decode()
    assert set(re.findall(r'^depend = (.+)$',payload['.PKGINFO'].decode(),re.M))==native
    assert 'pkgbuild_sha256sum = '+sha((root/'PKGBUILD').read_bytes())+'\n' in payload['.BUILDINFO'].decode()
    package='clang64/lib/dotnet/library-packs/'+name+'.4.61.3.nupkg';assert [p for p in payload if p.endswith('.nupkg')]==[package]
    with zipfile.ZipFile(io.BytesIO(payload[package])) as z:
        data=z.read('source-build/inventory.json');inventories.append(data);r=json.loads(data);project=r['projects'][name]
        assert r['source_commit']=='88df64013795d4e3716e1f677600f8164ffdb542' and r['source_archive_sha256']==hashes[0] and r['production_cs_changes']==[]
        assert r['original_project_trees']=={key:value['original_project_tree_sha256'] for key,value in inputs.items()}
        for tree in r['original_project_trees'].values():
            for path,digest in tree.items():assert sha(original[path])==digest,path
        for path,digest in r['original_project_metadata'].items():assert sha(original[path])==digest,path
        assert sha(original['build/MSAL.snk'])==r['original_signing_key_sha256']
        assert len(project['actual_original_compile_items'])==cs_count and len(project['generated_sdk_metadata'])==2
        for path,digest in project['actual_original_compile_items'].items():assert sha(original[path])==digest,path
        prefix='src/client/'+name+'/'
        all_cs={p for p in original if p.startswith(prefix) and p.endswith('.cs')}
        if suffix=='msal':
            expected={p for p in all_cs if not p.startswith(prefix+'Platforms/') and not (p.startswith(prefix+'Resources/') and '/' not in p[len(prefix+'Resources/'):])}
            expected.discard(prefix+'PlatformsCommon/PlatformNotSupported/ApiConfig/SystemWebViewOptions.cs')
            expected|={p for p in all_cs if any(p.startswith(prefix+'Platforms/'+v+'/') for v in ['netcore','net6','Features/DefaultOSBrowser','Features/DesktopOS','Features/OpenTelemetry'])}
            expected={p for p in expected if not p.startswith(prefix+'json/')}
            assert {'NET_CORE','SUPPORTS_CONFIDENTIAL_CLIENT','SUPPORTS_CUSTOM_CACHE','SUPPORTS_BROKER','SUPPORTS_WIN32','SUPPORTS_OTEL','SUPPORTS_SYSTEM_TEXT_JSON'}<=set(project['original_feature_flags'])
            generator,=project['json_source_generators'];assert generator['path']=='system.text.json/8.0.5/analyzers/dotnet/roslyn4.4/cs/System.Text.Json.SourceGeneration.dll'
            assert generator['sha256']==json_inventory['projects']['Gen4.4']['source_outputs']['System.Text.Json.SourceGeneration.dll']
            assert project['json_generated_files']
        else:expected=all_cs
        assert expected==set(project['actual_original_compile_items'])
        refs=project['actual_references'];assert all(v['origin'] in ['source-cache','framework','original-source-project'] for v in refs)
        for ref in refs:
            if ref['origin']=='source-cache':
                dependency,version=ref['path'].split('/')[:2];assert {'microsoft.identitymodel.abstractions':'6.35.0','system.text.json':'8.0.5','system.security.cryptography.protecteddata':'8.0.0'}[dependency]==version
            if ref['origin']=='original-source-project':assert suffix=='msal-extensions' and ref['sha256']==r['projects']['Microsoft.Identity.Client']['implementation_sha256']
        assert {p for p in z.namelist() if p.endswith('.dll')}=={'lib/net8.0/'+name+'.dll','ref/net8.0/'+name+'.dll'}
        for path,field in [('lib/net8.0/'+name+'.dll','implementation_sha256'),('ref/net8.0/'+name+'.dll','reference_sha256'),('lib/net8.0/'+name+'.xml','documentation_sha256')]:assert sha(z.read(path))==project[field]
        assert project['implementation_sha256']!=project['reference_sha256']
        generated=project.get('json_generated_files',{});assert {p for p in z.namelist() if p.startswith('source-build/generated/')}=={'source-build/generated/'+p for p in generated}
        for path,digest in generated.items():assert sha(z.read('source-build/generated/'+path))==digest
        assert z.read('LICENSE.txt')==original['LICENSE']==payload['clang64/share/licenses/dotnet-'+suffix+'/LICENSE']
        assert z.read('ThirdPartyNotice.txt')==original['ThirdPartyNotice.txt']==payload['clang64/share/licenses/dotnet-'+suffix+'/ThirdPartyNotice.txt']
        assert z.read('README.md')==(root/'README.md').read_bytes()==payload['clang64/share/doc/dotnet-'+suffix+'/README.md']
        assert data==payload['clang64/share/doc/dotnet-'+suffix+'/source-inventory.json']
        meta=E.fromstring(z.read(name+'.nuspec')).find('{*}metadata')
        assert meta.findtext('{*}id')==name and meta.findtext('{*}version')=='4.61.3' and meta.findtext('{*}license')=='MIT'
        assert meta.find('{*}repository').get('commit')==r['source_commit']
        assert {v.get('id'):v.get('version') for v in meta.findall('{*}dependencies/{*}group/{*}dependency')}=={k:'['+v+']' for k,v in dependencies.items()}
        assert [v.get('file') for v in meta.findall('{*}references/{*}group/{*}reference')]==[name+'.dll']
        assert len(json.loads((root/(name+'.api.json')).read_text())['api'])==api_count
        gold=json.loads((root/(name+'.metadata.json')).read_text())[0]
        for key,value in gold['raw_resources'].items():
            assert suffix=='msal'
            directive=original[prefix+'Properties/Microsoft.Identity.Client.rd.xml'];assert base64.b64decode(value['data'])==directive.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
    records.append(dict(name=name,artifact=artifact.name,sha256=sha(artifact.read_bytes()),original_compile_items=cs_count,public_api_declarations=api_count,implementation_sha256=project['implementation_sha256'],reference_sha256=project['reference_sha256']))
assert inventories[0]==inventories[1]
Path('tests/powershell-deps/msal-artifacts-20261027.json').write_text(json.dumps(records,indent=2)+'\n')
print('PASS both MSAL artifacts: all17 pinned inputs, complete original source/compiler branches,449 C# inputs,4SDK metadata files, JSON generator/context hashes, implementation/ref/XML, exact dependencies, original licenses/notices/directives')
