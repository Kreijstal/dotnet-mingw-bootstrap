"""Independently audit the complete selected JSON8 library/generator source package."""
from pathlib import Path
import hashlib,io,json,re,subprocess,tarfile,xml.etree.ElementTree as E,zipfile

root=Path('mingw-w64-dotnet-system-text-json8');sha=lambda data:hashlib.sha256(data).hexdigest()
info=(root/'.SRCINFO').read_text()
inputs=re.findall(r'^\s+source = (.+)$',info,re.M);hashes=re.findall(r'^\s+sha256sums = (.+)$',info,re.M)
assert len(inputs)==len(hashes)==18
for name,digest in zip(inputs,hashes):assert digest!='SKIP' and sha((root/name.split('::')[0]).read_bytes())==digest,name
artifact=root/'mingw-w64-clang-x86_64-dotnet-system-text-json8-8.0.5-1-any.pkg.tar.zst'
p=subprocess.Popen(['zstd','-dc',str(artifact)],stdout=subprocess.PIPE)
with tarfile.open(fileobj=p.stdout,mode='r|') as t:payload={m.name.removeprefix('./'):t.extractfile(m).read() for m in t if m.isfile()}
while p.stdout.read(1024*1024):pass
assert p.wait()==0
assert 'pkgver = 8.0.5-1\n' in payload['.PKGINFO'].decode()
assert re.findall(r'^depend = (.+)$',payload['.PKGINFO'].decode(),re.M)==['mingw-w64-clang-x86_64-dotnet-runtime=8.0.131']
assert 'pkgbuild_sha256sum = '+sha((root/'PKGBUILD').read_bytes())+'\n' in payload['.BUILDINFO'].decode()
package='clang64/lib/dotnet/library-packs/System.Text.Json.8.0.5.nupkg'
assert [n for n in payload if n.endswith('.nupkg')]==[package]
with zipfile.ZipFile(io.BytesIO(payload[package])) as z:
    inventory=json.loads(z.read('source-build/inventory.json'))
    assert inventory['source_commit']=='81cabf2857a01351e5ab578947c7403a5b128ad1'
    assert inventory['source_archive_sha256']==hashes[0] and inventory['production_cs_changes']==[]
    assert inventory['actual_original_compile_items']==497
    assert set(inventory['projects'])=={'Lib','Gen3.11','Gen4.0','Gen4.4'}
    assert json.loads(payload['clang64/share/doc/dotnet-system-text-json8/source-inventory.json'])==inventory
    wanted={'LICENSE.TXT','THIRD-PARTY-NOTICES.TXT'}
    for record in inventory['projects'].values():
        for field in ['original_project_metadata','declared_original_cs_candidates','expected_original_compile_items','original_resource_sources']:
            wanted.update(record[field])
    original={}
    with tarfile.open(root/inputs[0].split('::')[0],mode='r|gz') as t:
        for member in t:
            if not member.isfile():continue
            name=member.name.split('/',1)[1]
            if name in wanted:original[name]=t.extractfile(member).read()
    assert set(original)==wanted
    expected_dlls={'ref/net8.0/System.Text.Json.dll'};total=0
    for folder,record in inventory['projects'].items():
        assert record['production_cs_changes']==[]
        for field in ['original_project_metadata','declared_original_cs_candidates','expected_original_compile_items','original_resource_sources']:
            for name,digest in record[field].items():assert sha(original[name])==digest,(folder,name)
        assert record['actual_original_compile_items']==record['expected_original_compile_items']
        assert len(record['actual_generated_compile_items'])==3
        assert record['actual_generated_compile_items']['SR.Generated.cs']==record['generated_sr_sha256']
        assert all(r['origin'] in ['framework','source-cache'] and not r['path'].endswith('/System.Text.Json.dll') for r in record['actual_references'])
        prefix='lib/net8.0/' if folder=='Lib' else 'analyzers/dotnet/roslyn'+record['variant']+'/cs/'
        for relative,digest in record['source_outputs'].items():
            expected_dlls.add(prefix+relative);assert sha(z.read(prefix+relative))==digest,(folder,relative)
        assert len(record['source_outputs'])==(1 if folder=='Lib' else 14)
        assert sha(z.read(prefix+record['assembly']+'.dll'))==record['implementation_sha256']
        total+=len(record['actual_original_compile_items'])
    assert total==497 and len(expected_dlls)==44
    assert {n for n in z.namelist() if n.endswith('.dll')}==expected_dlls
    assert sha(z.read('ref/net8.0/System.Text.Json.dll'))==inventory['projects']['Lib']['reference_sha256']
    assert inventory['projects']['Lib']['reference_sha256']!=inventory['projects']['Lib']['implementation_sha256']
    assert not any(n.endswith(('.exe','.pdb','.p7s')) for n in z.namelist())
    for name,native in [('LICENSE.TXT','LICENSE'),('THIRD-PARTY-NOTICES.TXT','THIRD-PARTY-NOTICES.TXT')]:
        assert z.read(name)==original[name]==payload['clang64/share/licenses/dotnet-system-text-json8/'+native]
    assert z.read('README.md')==(root/'README.md').read_bytes()==payload['clang64/share/doc/dotnet-system-text-json8/README.md']
    localization=json.loads(z.read('source-build/localization.json'))
    assert len(localization)==43 and sum(len(r['strings']) for r in localization)==1460
    baseline=json.loads((root/'published-metadata.json').read_text())
    for resource in localization:
        variant=inventory['projects'][resource['project']]['variant']
        parent='lib/net8.0' if variant is None else 'analyzers/dotnet/roslyn'+variant+'/cs'+('/'+resource['culture'] if resource['culture'] else '')
        name=inventory['projects'][resource['project']]['assembly']+('.resources' if resource['culture'] else '')+'.dll'
        meta=next(r for r in baseline if r['file'].replace('\\','/').endswith('/metadata-only/'+parent+'/'+name))
        assert meta['resources']=={resource['manifest']:resource['strings']}
    meta=E.fromstring(z.read('System.Text.Json.nuspec')).find('{*}metadata')
    assert meta.findtext('{*}id')=='System.Text.Json' and meta.findtext('{*}version')=='8.0.5'
    assert meta.findtext('{*}license')=='MIT' and meta.find('{*}repository').get('commit')==inventory['source_commit']
    groups=meta.findall('{*}dependencies/{*}group');assert len(groups)==1 and groups[0].get('targetFramework')=='net8.0' and len(groups[0])==0
    assert [r.get('file') for r in meta.findall('{*}references/{*}group/{*}reference')]==['System.Text.Json.dll']
    assert sum(len(v['api']) for v in json.loads((root/'published-api.json').read_text()).values())==1784
record=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),source_commit=inventory['source_commit'],
            implementation_sha256=inventory['projects']['Lib']['implementation_sha256'],reference_sha256=inventory['projects']['Lib']['reference_sha256'],
            original_compiler_items=497,source_dll_assets=43,packaged_dll_assets=44,public_protected_api_declarations=1784,
            resource_sets=43,resource_strings=1460,production_cs_changes=[])
Path('tests/powershell-deps/system-text-json8-artifact-20261027.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS JSON8 all18 pinned inputs,497 original compiler items,43 source DLLs, library/ref and complete generator/localization payload, metadata and licenses')
