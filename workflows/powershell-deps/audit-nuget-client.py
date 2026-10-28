"""Independently audit all eleven source NuGet.Client Core split packages."""
from pathlib import Path
import hashlib
import io
import json
import re
import subprocess
import tarfile
import xml.etree.ElementTree as E
import zipfile

root=Path('mingw-w64-dotnet-nuget-client')
sha=lambda data:hashlib.sha256(data).hexdigest()
info=(root/'.SRCINFO').read_text()
inputs=re.findall(r'^\s+source = (.+)$',info,re.M)
hashes=re.findall(r'^\s+sha256sums = (.+)$',info,re.M)
assert len(inputs)==len(hashes) and len(inputs)>=18
for name,digest in zip(inputs,hashes):
    assert digest!='SKIP' and sha((root/name.split('::')[0]).read_bytes())==digest,name
gold=json.loads((root/'source-inventory.json').read_text())
libraries=json.loads((root/'libraries.json').read_text())
assert set(gold['projects'])==set(libraries) and len(libraries)==11
api=json.loads((root/'published-api.json').read_text())
metadata={x['name']:x for x in json.loads((root/'published-metadata.json').read_text())}
assert sum(len(x['api']) for x in api.values())==7845
assert sum(len(v) for x in metadata.values() for v in x['resources'].values())==807
original={}
prefixes=tuple('src/NuGet.Core/'+name+'/' for name in libraries)
with tarfile.open(root/inputs[0].split('::')[0],mode='r|gz') as t:
    for member in t:
        if not member.isfile():continue
        name=member.name.split('/',1)[1]
        if name.startswith(prefixes) or name in gold['shared_cs'] or name in ['LICENSE.txt','build/NOTICES.txt',
                'keys/35MSSharedLib1024.snk','Directory.Packages.props','Directory.Build.targets']:
            original[name]=t.extractfile(member).read()
for x in metadata.values():assert x['public_key']==original['keys/35MSSharedLib1024.snk'].hex()
assert hashlib.sha1(original['keys/35MSSharedLib1024.snk']).digest()[-8:][::-1].hex()=='31bf3856ad364e35'

def unpack(path):
    process=subprocess.Popen(['zstd','-dc',str(path)],stdout=subprocess.PIPE)
    with tarfile.open(fileobj=process.stdout,mode='r|') as t:
        result={m.name.removeprefix('./'):t.extractfile(m).read() for m in t if m.isfile()}
    while process.stdout.read(1024*1024):pass
    assert process.wait()==0
    return result

external={'Microsoft.Extensions.FileProviders.Abstractions':'dotnet-fileproviders-abstractions6=6.0.0',
          'Microsoft.Extensions.FileSystemGlobbing':'dotnet-filesystem-globbing6=6.0.0',
          'Newtonsoft.Json':'dotnet-newtonsoft-json=13.0.4',
          'System.Security.Cryptography.Pkcs':'dotnet-cryptography-pkcs=8.0.31',
          'System.Security.Cryptography.ProtectedData':'dotnet-protecteddata=8.0.31'}
payload_cache={}
def get_payload(name):
    if name not in payload_cache:
        path=root/f"mingw-w64-clang-x86_64-{libraries[name]['suffix']}-6.9.1-1-any.pkg.tar.zst"
        payload_cache[name]=unpack(path)
    return payload_cache[name]

records={};total=0
for name,library in libraries.items():
    suffix=library['suffix']
    artifact=root/f'mingw-w64-clang-x86_64-{suffix}-6.9.1-1-any.pkg.tar.zst'
    payload=get_payload(name)
    pkginfo=payload['.PKGINFO'].decode()
    assert 'pkgver = 6.9.1-1\n' in pkginfo
    expected_native={'mingw-w64-clang-x86_64-dotnet-runtime=8.0.131'}
    expected_native.update('mingw-w64-clang-x86_64-'+libraries[p]['suffix']+'=6.9.1' for p in library['projects'])
    expected_native.update('mingw-w64-clang-x86_64-'+external[p] for p in library['packages'])
    assert set(re.findall(r'^depend = (.+)$',pkginfo,re.M))==expected_native,(name,pkginfo)
    srcinfo=next(block for block in info.split('\npkgname = ')[1:] if block.splitlines()[0]=='mingw-w64-clang-x86_64-'+suffix)
    assert set(re.findall(r'^\s+depends = (.+)$',srcinfo,re.M))==expected_native,(name,srcinfo)
    assert 'pkgbuild_sha256sum = '+sha((root/'PKGBUILD').read_bytes())+'\n' in payload['.BUILDINFO'].decode()
    nuget=f'clang64/lib/dotnet/library-packs/{name}.6.9.1.nupkg'
    assert [p for p in payload if p.endswith('.nupkg')]==[nuget]
    with zipfile.ZipFile(io.BytesIO(payload[nuget])) as z:
        assert {p for p in z.namelist() if p.endswith('.dll')}=={f'lib/net8.0/{name}.dll',f'ref/net8.0/{name}.dll'}
        assert not any(p.endswith(('.exe','.pdb','.p7s')) for p in z.namelist())
        record=json.loads(z.read('source-build/inventory.json'))
        assert record['source_commit']==gold['source_commit'] and record['source_archive_sha256']==gold['source_archive_sha256']
        assert record['production_cs_changes']==[] and record['project']==name
        project=f'src/NuGet.Core/{name}/{name}.csproj'
        assert record['original_project_sha256']==sha(original[project])
        main={p:sha(data) for p,data in original.items() if p.startswith(f'src/NuGet.Core/{name}/') and p.endswith('.cs')}
        assert main==record['original_cs']==gold['projects'][name]['original_cs']
        expected=main | gold['shared_cs']
        assert record['actual_original_compile_items']==expected
        for p,digest in expected.items():assert sha(original[p])==digest,p
        assert len(record['actual_generated_compile_items'])==2
        expected_resources=record['original_resx'] | record.get('original_embedded',{})
        assert set(record['actual_resources'])==set(expected_resources)
        for p,digest in expected_resources.items():
            assert sha(original[p])==digest
            wanted=original[p]
            if p in record['resource_overlays']:
                encoding='utf-8-sig' if wanted.startswith(b'\xef\xbb\xbf') else 'utf-8'
                wanted=re.sub(r'(<value(?:\s[^>]*)?>)(.*?)(</value>)',
                    lambda m:m[1]+m[2].replace('\r\n','\n').replace('\n','&#xD;&#xA;')+m[3],wanted.decode(encoding),flags=re.S).encode(encoding)
                overlay=record['resource_overlays'][p]
                assert overlay['original_sha256']==digest and overlay['prepared_sha256']==sha(wanted)
            assert record['actual_resources'][p]['sha256']==sha(wanted)
        for p,overlay in record['metadata_overlays'].items():
            if 'original_sha256' in overlay:assert sha(original[p])==overlay['original_sha256']
            else:assert p=='SourceInputs.targets' and overlay['owned_sha256']==sha((root/p).read_bytes())
        for reference in record['actual_references']:
            assert reference['origin'] in ['prepared','framework','source-cache']
            if reference['origin']=='prepared':
                parts=reference['path'].split('/')
                assert parts[0]=='artifacts' and parts[1] in libraries,reference
                peer=parts[1]
                paths={f'artifacts/{peer}/bin/Release/net8.0/{peer}.dll':f'lib/net8.0/{peer}.dll',
                       f'artifacts/{peer}/obj/Release/net8.0/ref/{peer}.dll':f'ref/net8.0/{peer}.dll'}
                assert reference['path'] in paths,reference
                # Original projects may select implementation metadata for a
                # ProjectReference. Compare the actual chosen bytes to the same
                # source sibling package; consumers independently require ref/.
                peer_zip=get_payload(peer)[f'clang64/lib/dotnet/library-packs/{peer}.6.9.1.nupkg']
                with zipfile.ZipFile(io.BytesIO(peer_zip)) as other:
                    assert sha(other.read(paths[reference['path']]))==reference['sha256'],reference
        assert sha(z.read(f'lib/net8.0/{name}.dll'))==record['implementation_sha256']
        assert sha(z.read(f'ref/net8.0/{name}.dll'))==record['reference_sha256']
        assert record['implementation_sha256']!=record['reference_sha256'],name
        assert sha(z.read(f'lib/net8.0/{name}.xml'))==record['documentation_sha256']
        for path,source_name in [('LICENSE.txt','LICENSE.txt'),('NOTICES.txt','build/NOTICES.txt')]:
            native='LICENSE' if path=='LICENSE.txt' else path
            assert z.read(path)==original[source_name]==payload[f'clang64/share/licenses/{suffix}/{native}']
        assert z.read('README.md')==(root/'README.md').read_bytes()==payload[f'clang64/share/doc/{suffix}/README.md']
        doc=json.loads(payload[f'clang64/share/doc/{suffix}/source-inventory.json'])
        assert set(doc['projects'])=={name}
        for key,value in doc['projects'][name].items():assert record[key]==value,(name,key)
        meta=E.fromstring(z.read(name+'.nuspec')).find('{*}metadata')
        assert meta.findtext('{*}id')==name and meta.findtext('{*}version')=='6.9.1'
        assert meta.findtext('{*}license')=='Apache-2.0'
        assert meta.find('{*}repository').get('commit')==gold['source_commit']
        expected_deps={**{p:'6.9.1' for p in library['projects']},**library['packages']}
        actual_deps={d.get('id'):d.get('version') for d in meta.findall('{*}dependencies/{*}group/{*}dependency')}
        assert actual_deps=={p:'['+v+']' for p,v in expected_deps.items()}
        assert record['dependencies']==expected_deps
        refs=meta.findall('{*}references/{*}group')
        assert len(refs)==1 and refs[0].get('targetFramework')=='net8.0'
        assert [r.get('file') for r in refs[0]]==[name+'.dll']
    total+=len(expected)
    records[name]=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),
                      original_compile_items=len(expected),api_declarations=len(api[name]['api']),
                      implementation_sha256=record['implementation_sha256'],reference_sha256=record['reference_sha256'])
assert total==1288,total
Path('tests/powershell-deps/nuget-client-artifacts-20261027.json').write_text(json.dumps(records,indent=2)+'\n')
print('PASS eleven NuGet source packages, all pinned inputs,1288 original compiler items, source implementation/reference bytes, complete dependencies, resources, docs and Apache licenses/notices')
