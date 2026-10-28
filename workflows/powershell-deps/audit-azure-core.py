"""Independently audit the complete pinned Azure.Core source and packaged payloads."""
from pathlib import Path
import hashlib,io,json,re,subprocess,tarfile,xml.etree.ElementTree as E,zipfile

root=Path('mingw-w64-dotnet-azure-core');name='Azure.Core';sha=lambda b:hashlib.sha256(b).hexdigest()
info=(root/'.SRCINFO').read_text();sources=re.findall(r'^\s+source = (.+)$',info,re.M);hashes=re.findall(r'^\s+sha256sums = (.+)$',info,re.M)
assert len(sources)==len(hashes)==15
for source,digest in zip(sources,hashes):
    with (root/source.split('::')[0]).open('rb') as file:actual=hashlib.file_digest(file,'sha256').hexdigest()
    assert digest!='SKIP' and actual==digest,source
artifact=root/'mingw-w64-clang-x86_64-dotnet-azure-core-1.38.0-1-any.pkg.tar.zst'
p=subprocess.Popen(['zstd','-dc',str(artifact)],stdout=subprocess.PIPE)
with tarfile.open(fileobj=p.stdout,mode='r|') as t:payload={m.name.removeprefix('./'):t.extractfile(m).read() for m in t if m.isfile()}
while p.stdout.read(1024*1024):pass
assert p.wait()==0
assert 'pkgver = 1.38.0-1\n' in payload['.PKGINFO'].decode()
assert set(re.findall(r'^depend = (.+)$',payload['.PKGINFO'].decode(),re.M))=={
    'mingw-w64-clang-x86_64-dotnet-runtime=8.0.131','mingw-w64-clang-x86_64-dotnet-system-text-json8=8.0.5',
    'mingw-w64-clang-x86_64-dotnet-memory-data=1.0.2','mingw-w64-clang-x86_64-dotnet-clientmodel=1.0.0'}
assert 'pkgbuild_sha256sum = '+sha((root/'PKGBUILD').read_bytes())+'\n' in payload['.BUILDINFO'].decode()
package='clang64/lib/dotnet/library-packs/'+name+'.1.38.0.nupkg'
assert [p for p in payload if p.endswith('.nupkg')]==[package]
with zipfile.ZipFile(io.BytesIO(payload[package])) as z:
    r=json.loads(z.read('source-build/inventory.json'))
    assert r['source_commit']=='dae9516a40d0661a3351c3f6ddf0e0eb85ea2f6b' and r['source_archive_sha256']==hashes[0]
    assert r['production_cs_changes']==[] and len(r['original_cs'])==193 and len(r['original_project_tree'])==247
    assert r['original_project_tree']==json.loads((root/'source-inputs.json').read_text())['original_project_tree_sha256']
    wanted=set(r['original_project_metadata'])|set(r['original_project_tree'])|{'eng/AzureSDKClient.snk','LICENSE.txt'};original={}
    with tarfile.open(root/sources[0].split('::')[0],mode='r|gz') as t:
        for member in t:
            if not member.isfile():continue
            path=member.name.split('/',1)[1]
            if path in wanted:original[path]=t.extractfile(member).read()
    assert set(original)==wanted
    for mapping in ['original_project_metadata','original_project_tree','original_cs']:
        for path,digest in r[mapping].items():assert sha(original[path])==digest,path
    assert sha(original['eng/AzureSDKClient.snk'])==r['signing_key_sha256']
    assert r['actual_original_compile_items']==r['original_cs'] and len(r['generated_sdk_metadata'])==2
    project=E.fromstring(original['sdk/core/Azure.Core/src/Azure.Core.csproj'])
    included=[v.get('Include').replace('\\','/') for v in project.iter('Compile') if v.get('Include')]
    assert included==r['original_selected_shared'] and len(included)==32
    base='sdk/core/Azure.Core/src/'
    selected={k for k in r['original_project_tree'] if k.endswith('.cs') and not k.startswith(base+'Shared/')}|{base+k for k in included}
    assert selected==set(r['original_cs'])
    assert len(r['json_source_generators'])==1
    generator=r['json_source_generators'][0]
    assert generator['path']=='system.text.json/8.0.5/analyzers/dotnet/roslyn4.4/cs/System.Text.Json.SourceGeneration.dll'
    json_inventory=json.loads(Path('tests/powershell-deps/system-text-json8-source-inventory-20261027.json').read_text())
    assert generator['sha256']==json_inventory['projects']['Gen4.4']['source_outputs']['System.Text.Json.SourceGeneration.dll']
    assert r['json_generated_files']
    assert {k for k in z.namelist() if k.startswith('source-build/generated/')}=={'source-build/generated/'+k for k in r['json_generated_files']}
    for path,digest in r['json_generated_files'].items():assert sha(z.read('source-build/generated/'+path))==digest

    dependencies={'System.ClientModel':'1.0.0','System.Memory.Data':'1.0.2','System.Text.Json':'8.0.5'}
    assert r['source_dependencies']==dependencies
    assert all(v['origin'] in ['source-cache','framework'] for v in r['actual_references'])
    for dependency,version in dependencies.items():
        refs=[v for v in r['actual_references'] if v['path'].endswith('/'+dependency+'.dll')]
        assert len(refs)==1 and refs[0]['origin']=='source-cache' and refs[0]['path']==dependency.lower()+'/'+version+'/ref/net8.0/'+dependency+'.dll'
    assert {p for p in z.namelist() if p.endswith('.dll')}=={'lib/net8.0/'+name+'.dll','ref/net8.0/'+name+'.dll'}
    assert sha(z.read('lib/net8.0/'+name+'.dll'))==r['implementation_sha256']
    assert sha(z.read('ref/net8.0/'+name+'.dll'))==r['reference_sha256']
    assert sha(z.read('lib/net8.0/'+name+'.xml'))==r['documentation_sha256']
    assert r['implementation_sha256']!=r['reference_sha256']
    assert z.read('LICENSE.txt')==original['LICENSE.txt']==payload['clang64/share/licenses/dotnet-azure-core/LICENSE']
    assert z.read('README.md')==(root/'README.md').read_bytes()==payload['clang64/share/doc/dotnet-azure-core/README.md']
    assert z.read('source-build/inventory.json')==payload['clang64/share/doc/dotnet-azure-core/source-inventory.json']
    meta=E.fromstring(z.read(name+'.nuspec')).find('{*}metadata')
    assert meta.findtext('{*}id')==name and meta.findtext('{*}version')=='1.38.0'
    assert meta.findtext('{*}license')=='MIT' and meta.find('{*}repository').get('commit')==r['source_commit']
    assert {d.get('id'):d.get('version') for d in meta.findall('{*}dependencies/{*}group/{*}dependency')}=={k:'['+v+']' for k,v in dependencies.items()}
    assert [v.get('file') for v in meta.findall('{*}references/{*}group/{*}reference')]==[name+'.dll']
    assert len(json.loads((root/'published-api.json').read_text())['api'])==914
record=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),source_commit=r['source_commit'],original_compile_items=193,
            public_protected_api_declarations=914,production_cs_changes=[],implementation_sha256=r['implementation_sha256'],reference_sha256=r['reference_sha256'])
Path('tests/powershell-deps/azure-core-artifact-20261027.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS Azure.Core all15 pinned inputs, full source archive/project/compiler hashes, original Shared selection, source JSON generator/output hashes, all three source dependency references, implementation/ref/XML, native/NuGet dependencies and licenses')
