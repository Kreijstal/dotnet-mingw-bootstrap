"""Independently audit the complete pinned ClientModel source and packaged payloads."""
from pathlib import Path
import hashlib,io,json,re,subprocess,tarfile,xml.etree.ElementTree as E,zipfile

root=Path('mingw-w64-dotnet-clientmodel');name='System.ClientModel';sha=lambda b:hashlib.sha256(b).hexdigest()
info=(root/'.SRCINFO').read_text();sources=re.findall(r'^\s+source = (.+)$',info,re.M);hashes=re.findall(r'^\s+sha256sums = (.+)$',info,re.M)
assert len(sources)==len(hashes)==15
for source,digest in zip(sources,hashes):
    with (root/source.split('::')[0]).open('rb') as file:actual=hashlib.file_digest(file,'sha256').hexdigest()
    assert digest!='SKIP' and actual==digest,source
artifact=root/'mingw-w64-clang-x86_64-dotnet-clientmodel-1.0.0-1-any.pkg.tar.zst'
p=subprocess.Popen(['zstd','-dc',str(artifact)],stdout=subprocess.PIPE)
with tarfile.open(fileobj=p.stdout,mode='r|') as t:payload={m.name.removeprefix('./'):t.extractfile(m).read() for m in t if m.isfile()}
while p.stdout.read(1024*1024):pass
assert p.wait()==0
assert 'pkgver = 1.0.0-1\n' in payload['.PKGINFO'].decode()
assert set(re.findall(r'^depend = (.+)$',payload['.PKGINFO'].decode(),re.M))=={
    'mingw-w64-clang-x86_64-dotnet-runtime=8.0.131','mingw-w64-clang-x86_64-dotnet-system-text-json8=8.0.5',
    'mingw-w64-clang-x86_64-dotnet-memory-data=1.0.2'}
assert 'pkgbuild_sha256sum = '+sha((root/'PKGBUILD').read_bytes())+'\n' in payload['.BUILDINFO'].decode()
package='clang64/lib/dotnet/library-packs/'+name+'.1.0.0.nupkg'
assert [p for p in payload if p.endswith('.nupkg')]==[package]
with zipfile.ZipFile(io.BytesIO(payload[package])) as z:
    r=json.loads(z.read('source-build/inventory.json'))
    assert r['source_commit']=='8ffa7d7f26bb2c5e3dadf74b2aa9c9ba9c9d9208' and r['source_archive_sha256']==hashes[0]
    assert r['production_cs_changes']==[] and len(r['original_cs'])==11 and len(r['original_project_tree'])==12
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
    dependencies={'System.Memory.Data':'1.0.2','System.Text.Json':'8.0.5'}
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
    assert z.read('LICENSE.txt')==original['LICENSE.txt']==payload['clang64/share/licenses/dotnet-clientmodel/LICENSE']
    assert z.read('README.md')==(root/'README.md').read_bytes()==payload['clang64/share/doc/dotnet-clientmodel/README.md']
    assert z.read('source-build/inventory.json')==payload['clang64/share/doc/dotnet-clientmodel/source-inventory.json']
    meta=E.fromstring(z.read(name+'.nuspec')).find('{*}metadata')
    assert meta.findtext('{*}id')==name and meta.findtext('{*}version')=='1.0.0'
    assert meta.findtext('{*}license')=='MIT' and meta.find('{*}repository').get('commit')==r['source_commit']
    assert {d.get('id'):d.get('version') for d in meta.findall('{*}dependencies/{*}group/{*}dependency')}=={k:'['+v+']' for k,v in dependencies.items()}
    assert [v.get('file') for v in meta.findall('{*}references/{*}group/{*}reference')]==[name+'.dll']
    assert len(json.loads((root/'published-api.json').read_text())['api'])==21
record=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),source_commit=r['source_commit'],original_compile_items=11,
            public_protected_api_declarations=21,production_cs_changes=[],implementation_sha256=r['implementation_sha256'],reference_sha256=r['reference_sha256'])
Path('tests/powershell-deps/clientmodel-artifact-20261027.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS ClientModel all15 pinned inputs, full source archive/project/compiler hashes, both source dependency references, implementation/ref/XML, native/NuGet dependencies and licenses')
