"""Independent source/archive/package audit for the complete PSReadLine Core module."""
from pathlib import Path
import hashlib
import io
import json
import re
import subprocess
import tarfile
import xml.etree.ElementTree as E
import zipfile

sha=lambda data:hashlib.sha256(data).hexdigest()
root=Path('mingw-w64-powershell-psreadline')
info=(root/'.SRCINFO').read_text()
sources=re.findall(r'^\s+source = (.+)$',info,re.M)
hashes=re.findall(r'^\s+sha256sums = (.+)$',info,re.M)
assert len(sources)==len(hashes)==15
for name,digest in zip(sources,hashes):assert sha((root/name.split('::')[0]).read_bytes())==digest,name
with tarfile.open(root/'psreadline-2.3.6.tar.gz') as tar:
    files=[p for p in tar if p.isfile()];prefix=files[0].name.split('/')[0]+'/'
    original={p.name.removeprefix(prefix):tar.extractfile(p).read() for p in files}
golden=json.loads((root/'published-provenance.json').read_text())
assert sha((root/'psreadline-2.3.6.tar.gz').read_bytes())==golden['source_archive_sha256']
for folder,key,count in [('PSReadLine','original_cs_sources',48),('Polyfill','original_polyfill_cs_sources',1)]:
    actual={name:sha(data) for name,data in original.items() if name.startswith(folder+'/') and name.endswith('.cs')}
    assert len(actual)==count and actual==golden[key]
assert sha(original['PSReadLine/PSReadLineResources.resx'])==golden['resource_sha256']
artifact=root/'mingw-w64-clang-x86_64-powershell-psreadline-2.3.6-1-any.pkg.tar.zst'
process=subprocess.Popen(['zstd','-dc',str(artifact)],stdout=subprocess.PIPE)
with tarfile.open(fileobj=process.stdout,mode='r|') as tar:
    package={p.name.removeprefix('./'):tar.extractfile(p).read() for p in tar if p.isfile()}
while process.stdout.read(1024*1024):pass
assert process.wait()==0
nuget=package['clang64/lib/dotnet/library-packs/psreadline.2.3.6.nupkg']
with zipfile.ZipFile(io.BytesIO(nuget)) as z:assets={name:z.read(name) for name in z.namelist()}
expected_dlls={'Microsoft.PowerShell.PSReadLine2.dll','Microsoft.PowerShell.Pager.dll','net6plus/Microsoft.PowerShell.PSReadLine.Polyfiller.dll'}
assert {name for name in assets if name.endswith('.dll')}==expected_dlls
assert not any(name.endswith(('.pdb','.exe','.p7s')) for name in assets)
record=json.loads(assets['source-build/provenance.json'])
assert record['source_commit']==golden['source_commit']=='d2e770f93b7a53d8660a6402eb29d1ae1c35e767'
assert record['source_archive_sha256']==golden['source_archive_sha256'] and record['production_csharp_changes']==0
for key in ('original_cs_sources','original_polyfill_cs_sources','resource_sha256','original_module_assets'):assert record[key]==golden[key],key
assert record['bundled_pager_source_package']=='Microsoft.PowerShell.Pager/1.0.0'
for name,digest in golden['original_module_assets'].items():
    assert sha(original[name])==digest
    assert assets[Path(name).name]==original[name]
expected={Path(name).name for name in golden['original_module_assets']}|expected_dlls
assert set(record['module_files'])==expected and len(expected)==9
for name,digest in record['module_files'].items():
    assert sha(assets[name])==digest
    assert package['clang64/lib/powershell/Modules/PSReadLine/2.3.6/'+name]==assets[name]
assert assets['README.md']==(root/'README.md').read_bytes()
assert package['clang64/share/licenses/powershell-psreadline/License.txt']==original['License.txt']
pager=json.loads(Path('tests/powershell-deps/pager-artifact-20261027.json').read_text())
assert sha(assets['Microsoft.PowerShell.Pager.dll'])==pager['source_dll_sha256']
assert sha(assets['Microsoft.PowerShell.PSReadLine2.dll'])!=golden['published_dll_sha256']
tree=E.fromstring(assets['PSReadLine.nuspec']);ns={'n':tree.tag[1:].split('}')[0]};meta=tree.find('n:metadata',ns)
assert meta.findtext('n:id',namespaces=ns)=='PSReadLine' and meta.findtext('n:version',namespaces=ns)=='2.3.6'
assert not meta.findall('.//n:dependency',ns)
assert meta.find('n:license',ns).text=='MIT'
assert meta.find('n:repository',ns).get('commit')==golden['source_commit']
metadata=json.loads(Path('tests/powershell-deps/psreadline-metadata-20261027.json').read_text())
for name,digest in metadata['source_dll_sha256'].items():assert digest==sha(assets[name]),name
assert metadata['api']==golden['published_api'] and len(metadata['api']['api'])==457
fields=('name','version','public_key_token','attributes')
assert {k:metadata['main_identity'][k] for k in fields}==golden['published_identity']
forwarders=metadata['forwarder_identity']
assert {k:forwarders[k] for k in fields}=={k:golden['published_forwarder_identity'][k] for k in fields}
assert set(forwarders['types'])=={'.<Module>'}
assert len(forwarders['exported_types'])==5
expected_types=golden['published_forwarder_identity']['exported_types']
assert [{k:v for k,v in item.items() if k!='target_version'} for item in forwarders['exported_types']]==[{k:v for k,v in item.items() if k!='target_version'} for item in expected_types]
assert all(item['target_version']=='7.4.6.500' for item in forwarders['exported_types'])
resources={p.get('name'):p.findtext('value') for p in E.fromstring(original['PSReadLine/PSReadLineResources.resx']).findall('data')}
assert resources==metadata['resources'] and len(resources)==245
assert metadata['original_mock_console_sha256']==sha(original['test/MockConsole.cs'])
assert 'pkgver = 2.3.6-1' in package['.PKGINFO'].decode()
assert 'depend = mingw-w64-clang-x86_64-dotnet-powershell-pager=1.0.0' in package['.PKGINFO'].decode()
result=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),nuget_sha256=sha(nuget),pinned_inputs=15,
    original_main_cs=48,original_core_polyfill_cs=1,production_csharp_changes=0,source_commit=golden['source_commit'],
    module_files=9,source_dll_assets=3,public_api_entries=457,resource_strings=245,core_type_forwarders=5,
    source_dll_sha256=metadata['source_dll_sha256'],loose_nuget_bytes_equal=True,bundled_pager_byte_exact=True)
Path('tests/powershell-deps/psreadline-artifact-20261027.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS PSReadLine:15 pinned inputs,48+1 unchanged source files,457 APIs,245 strings,5 Core forwarders,3 source DLLs and full9-file NuGet/loose module/license parity')
