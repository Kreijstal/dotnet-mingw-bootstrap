"""Independent ThreadJob source/package audit; published DLL remains metadata-only."""
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

sha=lambda data:hashlib.sha256(data).hexdigest()
root=Path('mingw-w64-powershell-threadjob')
info=(root/'.SRCINFO').read_text()
sources=re.findall(r'^\s+source = (.+)$',info,re.M)
hashes=re.findall(r'^\s+sha256sums = (.+)$',info,re.M)
assert len(sources)==len(hashes)==16
for name,digest in zip(sources,hashes):assert sha((root/name.split('::')[0]).read_bytes())==digest,name
with tarfile.open(root/'threadjob-2.0.3.tar.gz') as tar:
    members=[p for p in tar if p.isfile()]
    prefix=members[0].name.split('/')[0]+'/'
    original={p.name.removeprefix(prefix):tar.extractfile(p).read() for p in members}
golden=json.loads((root/'published-provenance.json').read_text())
assert sha((root/'threadjob-2.0.3.tar.gz').read_bytes())==golden['source_archive_sha256']
source_files={name:sha(data) for name,data in original.items() if name.startswith('PSThreadJob/') and name.endswith('.cs')}
assert len(source_files)==3 and source_files==golden['source_files']
assert sha(original['PSThreadJob/Resources.resx'])==golden['resource_sha256']
assert sha(original['PSThreadJob/ThreadJob.psd1'])==golden['source_manifest_sha256']
artifact=root/'mingw-w64-clang-x86_64-powershell-threadjob-2.0.3-1-any.pkg.tar.zst'
process=subprocess.Popen(['zstd','-dc',str(artifact)],stdout=subprocess.PIPE)
with tarfile.open(fileobj=process.stdout,mode='r|') as tar:
    package={p.name.removeprefix('./'):tar.extractfile(p).read() for p in tar if p.isfile()}
while process.stdout.read(1024*1024):pass
assert process.wait()==0
nuget=package['clang64/lib/dotnet/library-packs/threadjob.2.0.3.nupkg']
with zipfile.ZipFile(io.BytesIO(nuget)) as z:assets={name:z.read(name) for name in z.namelist()}
assert {name for name in assets if name.endswith('.dll')}=={'Microsoft.PowerShell.ThreadJob.dll'}
assert not any(name.endswith(('.exe','.pdb','.p7s')) for name in assets)
assert assets['ThreadJob.psd1']==original['PSThreadJob/ThreadJob.psd1']
assert assets['LICENSE']==package['clang64/share/licenses/powershell-threadjob/LICENSE']==original['LICENSE']
assert assets['README.md']==(root/'README.md').read_bytes()
tree=E.fromstring(assets['ThreadJob.nuspec']);ns={'n':tree.tag[1:].split('}')[0]}
meta=tree.find('n:metadata',ns)
assert meta.findtext('n:id',namespaces=ns)=='ThreadJob'
assert meta.findtext('n:version',namespaces=ns)=='2.0.3'
assert meta.find('n:dependencies',ns) is None
assert meta.find('n:license',ns).text=='MIT'
assert meta.find('n:repository',ns).get('commit')==golden['source_commit']
record=json.loads(assets['source-build/provenance.json'])
assert record['source_files']==source_files
assert record['resource_sha256']==golden['resource_sha256']
assert record['source_commit']==golden['source_commit']
assert record['source_archive_sha256']==golden['source_archive_sha256']
assert len(record['source_algorithm_overlays'])==1
overlay=record['source_algorithm_overlays'][0]
patch=root/'0001-use-current-debugger-setbreakpoints-overload.patch'
assert overlay['patch_sha256']==sha(patch.read_bytes())
with tempfile.TemporaryDirectory() as temp:
    path=Path(temp)/'PSThreadJob.cs'
    path.write_text(original['PSThreadJob/PSThreadJob.cs'].decode('utf-8-sig').replace('\r\n','\n'))
    subprocess.run(['patch','--fuzz=0','-p1','-i',str(patch.resolve())],cwd=temp,check=True,stdout=subprocess.PIPE)
    assert sha(path.read_bytes())==overlay['overlay_sha256']
assert set(record['module_files'])=={'Microsoft.PowerShell.ThreadJob.dll','ThreadJob.psd1'}
for name,digest in record['module_files'].items():
    assert sha(assets[name])==digest
    assert package['clang64/lib/powershell/Modules/ThreadJob/2.0.3/'+name]==assets[name]
assert sum(name.endswith('.dll') for name in package)==1
assert sha(assets['Microsoft.PowerShell.ThreadJob.dll'])!=golden['published_dll_sha256']
metadata=json.loads(Path('tests/powershell-deps/threadjob-metadata-20261027.json').read_text())
assert metadata['source_dll_sha256']==sha(assets['Microsoft.PowerShell.ThreadJob.dll'])
assert metadata['api']==golden['published_api'] and len(metadata['api']['api'])==55
assert {k:v for k,v in metadata['identity'].items() if k!='types'}==golden['published_identity']
pkginfo=package['.PKGINFO'].decode()
assert 'pkgver = 2.0.3-1' in pkginfo
assert 'depend = mingw-w64-clang-x86_64-dotnet-runtime=8.0.131' in pkginfo
result=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),nuget_sha256=sha(nuget),
    source_dll_sha256=sha(assets['Microsoft.PowerShell.ThreadJob.dll']),pinned_inputs=16,
    source_commit=golden['source_commit'],original_cs_files=3,source_overlays=1,
    public_api_entries=55,assembly_version='2.0.1.0',module_nuget_version='2.0.3',
    loose_nuget_bytes_equal=True,source_reference='complete original SMA7.4.20',
    constrained_language_machine_policy_cases_verified=False)
Path('tests/powershell-deps/threadjob-artifact-20261027.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS ThreadJob:16 pinned inputs, complete original sources/resource, exact debugger overlay,55-entry metadata gate, sole source DLL, original manifest/license and NuGet/loose module parity')
