"""Independent pinned-source and three-package audit for Extensions6 prerequisites."""
from pathlib import Path
import hashlib
import io
import json
import re
import subprocess
import tarfile
import xml.etree.ElementTree as E
import zipfile

root=Path('mingw-w64-dotnet-extensions6')
sha=lambda data:hashlib.sha256(data).hexdigest()
info=(root/'.SRCINFO').read_text()
inputs=re.findall(r'^\s+source = (.+)$',info,re.M)
hashes=re.findall(r'^\s+sha256sums = (.+)$',info,re.M)
assert len(inputs)==len(hashes)==13
for name,digest in zip(inputs,hashes):assert sha((root/name.split('::')[0]).read_bytes())==digest,name
metadata={item['name']:item for item in json.loads((root/'published-metadata.json').read_text())}
api=json.loads((root/'published-api.json').read_text())
gold=json.loads((root/'published-provenance.json').read_text())
assert sum(len(item['api']) for item in api.values())==382
assert sum(len(values) for item in metadata.values() for values in item['resources'].values())==20
key=bytes.fromhex((root/'AspNetCore.pubkey.hex').read_text())
assert hashlib.sha1(key).digest()[-8:][::-1].hex()=='adb9793829ddae60'
for item in metadata.values():assert item['public_key']==key.hex()
prefixes=tuple('src/libraries/'+name+'/src/' for name in metadata)
shared={'src/libraries/Common/src/System/SR.cs','src/libraries/Common/src/Extensions/EmptyDisposable.cs',
        'src/libraries/System.Private.CoreLib/src/System/Numerics/Hashing/HashHelpers.cs'}
original={}
with tarfile.open(root/inputs[0].split('::')[0],mode='r|gz') as t:
    for member in t:
        if not member.isfile():continue
        name=member.name.split('/',1)[1]
        if name.startswith(prefixes) or name in shared or name=='LICENSE.TXT':
            original[name]=t.extractfile(member).read()

def package(path):
    process=subprocess.Popen(['zstd','-dc',str(path)],stdout=subprocess.PIPE)
    with tarfile.open(fileobj=process.stdout,mode='r|') as t:
        result={member.name.removeprefix('./'):t.extractfile(member).read() for member in t if member.isfile()}
    while process.stdout.read(1024*1024):pass
    assert process.wait()==0
    return result

suffixes={'Microsoft.Extensions.Primitives':'dotnet-extensions-primitives6',
          'Microsoft.Extensions.FileProviders.Abstractions':'dotnet-fileproviders-abstractions6',
          'Microsoft.Extensions.FileSystemGlobbing':'dotnet-filesystem-globbing6'}
records={};total=0
for name,suffix in suffixes.items():
    artifact=root/f'mingw-w64-clang-x86_64-{suffix}-6.0.0-1-any.pkg.tar.zst'
    payload=package(artifact)
    assert 'pkgver = 6.0.0-1' in payload['.PKGINFO'].decode()
    assert 'depend = mingw-w64-clang-x86_64-dotnet-runtime=8.0.131' in payload['.PKGINFO'].decode()
    if name.endswith('Abstractions'):
        assert 'depend = mingw-w64-clang-x86_64-dotnet-extensions-primitives6=6.0.0' in payload['.PKGINFO'].decode()
    with zipfile.ZipFile(io.BytesIO(payload[f'clang64/lib/dotnet/library-packs/{name}.6.0.0.nupkg'])) as z:
        names=z.namelist()
        assert not any(path.endswith(('.exe','.pdb','.p7s')) for path in names)
        assert {path for path in names if path.endswith('.dll')}=={f'lib/net8.0/{name}.dll',f'ref/net8.0/{name}.dll'}
        record=json.loads(z.read('source-build/inventory.json'))
        assert record['production_cs_changes']==[]
        assert record['published_provenance']==gold[name]
        assert record['original_project_sha256']==sha(original[record['original_project']])
        main={path:sha(data) for path,data in original.items() if path.startswith('src/libraries/'+name+'/src/') and path.endswith('.cs')}
        assert record['original_cs']==main
        expected=record['original_cs'] | record['shared_cs']
        assert record['actual_original_compile_items']==sorted(expected)
        assert len(record['actual_generated_compile_items'])==3
        for path,digest in expected.items():assert sha(original[path])==digest,path
        for path,digest in record['original_resources'].items():
            assert sha(original[path])==digest
            source_values={item.get('name'):item.findtext('value') for item in E.fromstring(original[path]).iter('data')}
            assert all(source_values==values for values in metadata[name]['resources'].values())
        assert record['expected_resource_sets']==metadata[name]['resources']
        assert sha(z.read(f'lib/net8.0/{name}.dll'))==record['implementation_sha256']
        assert sha(z.read(f'ref/net8.0/{name}.dll'))==record['reference_sha256']
        assert z.read('LICENSE.TXT')==original['LICENSE.TXT']==payload[f'clang64/share/licenses/{suffix}/LICENSE']
        assert z.read('README.md')==(root/'README.md').read_bytes()
        assert z.read('source-build/inventory.json')==payload[f'clang64/share/doc/{suffix}/source-inventory.json']
        tree=E.fromstring(z.read(name+'.nuspec'));ns={'n':tree.tag[1:].split('}')[0]}
        meta=tree.find('n:metadata',ns)
        assert meta.findtext('n:id',namespaces=ns)==name and meta.findtext('n:version',namespaces=ns)=='6.0.0'
        assert meta.find('n:license',ns).text=='MIT'
        assert meta.find('n:repository',ns).get('commit')=='4822e3c3aa77eb82b2fb33c9321f923cf11ddde6'
        refs=meta.findall('n:references/n:group',ns)
        assert len(refs)==1 and refs[0].get('targetFramework')=='net8.0'
        assert [item.get('file') for item in refs[0]]==[name+'.dll']
        deps=[(dep.get('id'),dep.get('version')) for dep in meta.findall('n:dependencies/n:group/n:dependency',ns)]
        assert deps==([('Microsoft.Extensions.Primitives','6.0.0')] if name.endswith('Abstractions') else []),deps
    total+=len(expected)
    records[name]=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),original_compile_inputs=len(expected),
        public_api_declarations=len(api[name]['api']),resource_manifests=2,resource_values=sum(len(v) for v in metadata[name]['resources'].values()),
        production_cs_changes=[],implementation_sha256=record['implementation_sha256'],reference_sha256=record['reference_sha256'])
assert total==56
Path('tests/powershell-deps/extensions6-artifacts-20261027.json').write_text(json.dumps(records,indent=2)+'\n')
print('PASS Extensions6 all13 pinned inputs,56 actual original compiler inputs,382-entry API baselines,20 resource values, source implementation/reference payloads, metadata and licenses')
