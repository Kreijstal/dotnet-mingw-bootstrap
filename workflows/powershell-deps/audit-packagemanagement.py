"""Independently verify all PackageManagement Core source and packaged payloads."""
from pathlib import Path
import hashlib
import io
import json
import re
import subprocess
import tarfile
import tempfile
import xml.etree.ElementTree as E
import zipfile

sha=lambda b:hashlib.sha256(b).hexdigest()
root=Path('mingw-w64-powershell-packagemanagement')
info=(root/'.SRCINFO').read_text();inputs=re.findall(r'^\s+source = (.+)$',info,re.M);hashes=re.findall(r'^\s+sha256sums = (.+)$',info,re.M)
assert len(inputs)==len(hashes)==19
for name,digest in zip(inputs,hashes):assert sha((root/name.split('::')[0]).read_bytes())==digest,name
def archive(path):
    with tarfile.open(path) as tar:
        files=[p for p in tar if p.isfile()];prefix=files[0].name.split('/')[0]+'/'
        return {p.name.removeprefix(prefix):tar.extractfile(p).read() for p in files}
source=archive(root/'oneget-1.4.8.1.tar.gz')
provider=archive(root/'nugetprovider-b4a79a22f65c85a5de7aa3f23c0505fb38422e1d.tar.gz')
golden=json.loads((root/'published-provenance.json').read_text())
assert sha((root/'oneget-1.4.8.1.tar.gz').read_bytes())==golden['source_archive_sha256']
assert sha((root/'nugetprovider-b4a79a22f65c85a5de7aa3f23c0505fb38422e1d.tar.gz').read_bytes())==golden['source_nuget_provider_archive_sha256']
artifact=root/'mingw-w64-clang-x86_64-powershell-packagemanagement-1.4.8.1-1-any.pkg.tar.zst'
process=subprocess.Popen(['zstd','-dc',str(artifact)],stdout=subprocess.PIPE)
with tarfile.open(fileobj=process.stdout,mode='r|') as tar:package={p.name.removeprefix('./'):tar.extractfile(p).read() for p in tar if p.isfile()}
while process.stdout.read(1024*1024):pass
assert process.wait()==0
nuget=package['clang64/lib/dotnet/library-packs/packagemanagement.1.4.8.1.nupkg']
with zipfile.ZipFile(io.BytesIO(nuget)) as z:assets={name:z.read(name) for name in z.namelist()}
record=json.loads(assets['source-build/provenance.json']);compiler=json.loads(assets['source-build/compile-inventory.json'])
assert record['source_commit']==golden['source_commit'] and record['provider_commit']==golden['source_nuget_provider_commit']
assert record['production_csharp_changes']==3 and len(record['projects'])==6
assert set(compiler)==set(record['projects'])
original_total=0
for name,item in record['projects'].items():
    if name.endswith('.NuGetProvider'):
        originals={'src/'+name+'/'+path:sha(data) for path,data in provider.items() if path.endswith('.cs')}
    else:
        originals={path:sha(data) for path,data in source.items() if path.startswith('src/'+name+'/') and path.endswith('.cs')}
    assert originals==item['original_cs_files'],name
    for path,digest in compiler[name]['original_sha256'].items():assert originals['src/'+name+'/'+path]==digest,(name,path)
    assert compiler[name]['original_compiled_sources']==len(compiler[name]['original_sha256'])
    assert {'CORECLR','DEBUG','NET8_0'}<=set(compiler[name]['constants'])
    if name.endswith('.CoreProviders'):assert 'COREv1' in compiler[name]['constants']
    original_total+=compiler[name]['original_compiled_sources']
assert original_total==277
assert sum(len(p['generated_sources']) for p in compiler.values())==5
overlay=record['production_source_overlays'];patch=root/'0002-qualify-original-async-enumerable.patch'
assert overlay['patch_sha256']==sha(patch.read_bytes()) and len(overlay['files'])==3
with tempfile.TemporaryDirectory() as temp:
    for entry in overlay['files']:
        relative=Path(entry['path']).relative_to('src');target=Path(temp)/relative;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(source[entry['path']].decode('utf-8-sig').replace('\r\n','\n'))
    subprocess.run(['patch','--fuzz=0','-p1','-i',str(patch.resolve())],cwd=temp,check=True,stdout=subprocess.PIPE)
    for entry in overlay['files']:assert sha((Path(temp)/Path(entry['path']).relative_to('src')).read_bytes())==entry['sha256']
generator=source['src/New-StronglyTypedCsFileForResx.ps1']
assert sha(generator)==record['resource_generator']['original_sha256']
with tempfile.TemporaryDirectory() as temp:
    path=Path(temp)/'New-StronglyTypedCsFileForResx.ps1';path.write_text(generator.decode('utf-8-sig').replace('\r\n','\n'))
    generator_patch=root/'0001-resource-generator-use-script-root-for-archives.patch'
    assert sha(generator_patch.read_bytes())==record['resource_generator']['patch_sha256']
    subprocess.run(['patch','--fuzz=0','-p1','-i',str(generator_patch.resolve())],cwd=temp,check=True,stdout=subprocess.PIPE)
    assert sha(path.read_bytes())==record['resource_generator']['staged_sha256']
metadata=json.loads(Path('tests/powershell-deps/packagemanagement-metadata-20261027.json').read_text())
count=0
for name,baseline in golden['metadata_only_baselines'].items():
    relative='coreclr/netstandard2.0/'+name
    assert sha(assets[relative])==metadata['assemblies'][name]['source_sha256']
    assert sha(assets[relative])!=baseline['published_dll_sha256']
    identity=metadata['assemblies'][name]['identity']
    assert {k:identity[k] for k in ('name','version','public_key_token','attributes')}==baseline['identity']
    assert identity['debuggable_flags']==263
    assert metadata['assemblies'][name]['api']==baseline['api']
    count+=len(baseline['api']['api'])
assert count==2205
assert len(record['module_files'])==21
assert {name for name in assets if name.endswith('.dll')}=={'coreclr/netstandard2.0/'+name for name in golden['metadata_only_baselines']}
for name,digest in record['module_files'].items():
    assert sha(assets[name])==digest,name
    assert package['clang64/lib/powershell/Modules/PackageManagement/1.4.8.1/'+name]==assets[name]
    if not name.endswith('.dll'):
        original='src/Microsoft.PowerShell.PackageManagement/'+name if not name.startswith('DSCResources/') else 'src/Microsoft.PackageManagement.DscResources/'+name.removeprefix('DSCResources/')
        assert assets[name]==source[original],name
assert assets['LICENSE']==package['clang64/share/licenses/powershell-packagemanagement/LICENSE']==source['LICENSE']
assert assets['NuGetProvider-LICENSE.txt']==package['clang64/share/licenses/powershell-packagemanagement/NuGetProvider-LICENSE.txt']==provider['LICENSE.txt']
assert assets['README.md']==(root/'README.md').read_bytes()
assert not any(name.endswith(('.pdb','.exe','.p7s')) for name in assets)
resources=json.loads(assets['source-build/expected-resources.json'])
assert sum(len(values) for item in resources.values() for values in item.values())==295
assert resources==metadata['resources']
tree=E.fromstring(assets['PackageManagement.nuspec']);ns={'n':tree.tag[1:].split('}')[0]};meta=tree.find('n:metadata',ns)
assert meta.findtext('n:id',namespaces=ns)=='PackageManagement' and meta.findtext('n:version',namespaces=ns)=='1.4.8.1'
assert meta.find('n:license',ns).text=='Apache-2.0' and not meta.findall('.//n:dependency',ns)
assert meta.find('n:repository',ns).get('commit')==golden['source_commit']
assert 'pkgver = 1.4.8.1-1' in package['.PKGINFO'].decode()
result=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),nuget_sha256=sha(nuget),pinned_inputs=19,
    original_compiled_cs=277,generated_resource_classes=5,resource_strings=295,public_api_entries=2205,
    source_assemblies=6,module_files=21,public_signing_token='31bf3856ad364e35',source_overlays=3,
    build_configuration='original published Debug flavor',loose_nuget_bytes_equal=True,
    source_dll_sha256={name:data['source_sha256'] for name,data in metadata['assemblies'].items()})
Path('tests/powershell-deps/packagemanagement-artifact-20261027.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS PackageManagement:19 pinned inputs,277 original source inputs/five generators, exact overlays,2205 APIs,295 strings, six source assemblies,21 module files and NuGet/loose/license parity')
