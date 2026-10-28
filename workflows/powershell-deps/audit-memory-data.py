"""Audit complete pinned BinaryData source, references, NuGet and native payloads."""
from pathlib import Path
import hashlib,io,json,re,subprocess,tarfile,xml.etree.ElementTree as E,zipfile

root=Path('mingw-w64-dotnet-memory-data');name='System.Memory.Data';sha=lambda b:hashlib.sha256(b).hexdigest()
info=(root/'.SRCINFO').read_text();sources=re.findall(r'^\s+source = (.+)$',info,re.M);hashes=re.findall(r'^\s+sha256sums = (.+)$',info,re.M)
assert len(sources)==len(hashes)==16
for source,digest in zip(sources,hashes):
    path=root/source.split('::')[0];h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    assert digest!='SKIP' and h.hexdigest()==digest,source
artifact=root/'mingw-w64-clang-x86_64-dotnet-memory-data-1.0.2-1-any.pkg.tar.zst'
p=subprocess.Popen(['zstd','-dc',str(artifact)],stdout=subprocess.PIPE)
with tarfile.open(fileobj=p.stdout,mode='r|') as t:payload={m.name.removeprefix('./'):t.extractfile(m).read() for m in t if m.isfile()}
while p.stdout.read(1024*1024):pass
assert p.wait()==0
assert 'pkgver = 1.0.2-1\n' in payload['.PKGINFO'].decode()
assert set(re.findall(r'^depend = (.+)$',payload['.PKGINFO'].decode(),re.M))=={'mingw-w64-clang-x86_64-dotnet-runtime=8.0.131','mingw-w64-clang-x86_64-dotnet-system-text-json8=8.0.5'}
assert 'pkgbuild_sha256sum = '+sha((root/'PKGBUILD').read_bytes())+'\n' in payload['.BUILDINFO'].decode()
package='clang64/lib/dotnet/library-packs/'+name+'.1.0.2.nupkg'
assert [p for p in payload if p.endswith('.nupkg')]==[package]
with zipfile.ZipFile(io.BytesIO(payload[package])) as z:
    r=json.loads(z.read('source-build/inventory.json'))
    assert r['source_commit']=='7e3cf643977591e9041f4c628fd4d28237398e0b' and r['source_archive_sha256']==hashes[0]
    assert r['production_cs_changes']==[] and len(r['original_cs'])==2
    wanted=set(r['original_project_metadata'])|set(r['original_cs'])|{x['path'] for x in r['git_tree_files']}|{'LICENSE.txt'};original={}
    with tarfile.open(root/sources[0].split('::')[0],mode='r|gz') as t:
        for member in t:
            if not member.isfile():continue
            path=member.name.split('/',1)[1]
            if path in wanted:original[path]=t.extractfile(member).read()
    assert set(original)==wanted
    for path,digest in r['original_project_metadata'].items():assert sha(original[path])==digest
    for path,digest in r['original_cs'].items():assert sha(original[path])==digest
    for item in r['git_tree_files']:
        data=original[item['path']];assert sha(data)==item['sha256']
        assert hashlib.sha1(('blob '+str(len(data))+'\0').encode()+data).hexdigest()==item['git_blob']
    assert r['actual_original_compile_items']==r['original_cs'] and len(r['generated_sdk_metadata'])==2
    json_refs=[v for v in r['actual_references'] if v['path'].endswith('/System.Text.Json.dll')]
    assert len(json_refs)==1 and json_refs[0]['origin']=='source-cache' and json_refs[0]['path'].startswith('system.text.json/8.0.5/')
    assert all(v['origin'] in ['source-cache','framework'] for v in r['actual_references'])
    assert {p for p in z.namelist() if p.endswith('.dll')}=={'lib/net8.0/'+name+'.dll','ref/net8.0/'+name+'.dll'}
    assert sha(z.read('lib/net8.0/'+name+'.dll'))==r['implementation_sha256']
    assert sha(z.read('ref/net8.0/'+name+'.dll'))==r['reference_sha256']
    assert sha(z.read('lib/net8.0/'+name+'.xml'))==r['documentation_sha256']
    assert r['implementation_sha256']!=r['reference_sha256']
    assert z.read('LICENSE.txt')==original['LICENSE.txt']==payload['clang64/share/licenses/dotnet-memory-data/LICENSE']
    assert z.read('README.md')==(root/'README.md').read_bytes()==payload['clang64/share/doc/dotnet-memory-data/README.md']
    assert z.read('source-build/inventory.json')==payload['clang64/share/doc/dotnet-memory-data/source-inventory.json']
    meta=E.fromstring(z.read(name+'.nuspec')).find('{*}metadata')
    assert meta.findtext('{*}id')==name and meta.findtext('{*}version')=='1.0.2'
    assert meta.findtext('{*}license')=='MIT' and meta.find('{*}repository').get('commit')==r['source_commit']
    assert [(d.get('id'),d.get('version')) for d in meta.findall('{*}dependencies/{*}group/{*}dependency')]==[('System.Text.Json','[8.0.5]')]
    assert [v.get('file') for v in meta.findall('{*}references/{*}group/{*}reference')]==[name+'.dll']
    assert len(json.loads((root/'published-api.json').read_text())['api'])==20
record=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),source_commit=r['source_commit'],original_compile_items=2,
            public_protected_api_declarations=20,production_cs_changes=[],implementation_sha256=r['implementation_sha256'],reference_sha256=r['reference_sha256'])
Path('tests/powershell-deps/memory-data-artifact-20261027.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS BinaryData all16 pinned inputs, full archive/Git-tree/compiler hashes, source JSON reference, implementation/ref/XML, native/NuGet dependencies and licenses')
