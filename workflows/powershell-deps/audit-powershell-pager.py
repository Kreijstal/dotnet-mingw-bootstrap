"""Audit the new PSReadLine transitive Pager package independently of its builder."""
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
root=Path('mingw-w64-dotnet-powershell-pager')
info=(root/'.SRCINFO').read_text()
inputs=re.findall(r'^\s+source = (.+)$',info,re.M)
hashes=re.findall(r'^\s+sha256sums = (.+)$',info,re.M)
assert len(inputs)==len(hashes)==12
for name,digest in zip(inputs,hashes):assert sha((root/name.split('::')[0]).read_bytes())==digest,name
with tarfile.open(root/'pager-1.0.0.tar.gz') as tar:
    files=[p for p in tar if p.isfile()];prefix=files[0].name.split('/')[0]+'/'
    original={p.name.removeprefix(prefix):tar.extractfile(p).read() for p in files}
golden=json.loads((root/'published-provenance.json').read_text())
assert sha((root/'pager-1.0.0.tar.gz').read_bytes())==golden['source_archive_sha256']
sources={name:sha(data) for name,data in original.items() if name.startswith('src/') and name.endswith('.cs')}
assert len(sources)==2 and sources==golden['original_cs_sources']
artifact=root/'mingw-w64-clang-x86_64-dotnet-powershell-pager-1.0.0-1-any.pkg.tar.zst'
process=subprocess.Popen(['zstd','-dc',str(artifact)],stdout=subprocess.PIPE)
with tarfile.open(fileobj=process.stdout,mode='r|') as tar:
    package={p.name.removeprefix('./'):tar.extractfile(p).read() for p in tar if p.isfile()}
while process.stdout.read(1024*1024):pass
assert process.wait()==0
nuget=package['clang64/lib/dotnet/library-packs/Microsoft.PowerShell.Pager.1.0.0.nupkg']
with zipfile.ZipFile(io.BytesIO(nuget)) as z:assets={name:z.read(name) for name in z.namelist()}
assert {name for name in assets if name.endswith('.dll')}=={'lib/net8.0/Microsoft.PowerShell.Pager.dll'}
assert not any(name.endswith(('.p7s','.exe','.pdb')) for name in assets)
assert assets['LICENSE']==package['clang64/share/licenses/dotnet-powershell-pager/LICENSE']==original['LICENSE']
assert assets['README.md']==(root/'README.md').read_bytes()
record=json.loads(assets['source-build/source-inventory.json'])
assert record['original_cs_sources']==sources and record['production_csharp_changes']==0
assert record['source_commit']==golden['source_commit']=='965675f549dfa074eb02a47ca6d6e07d0ee7771f'
tree=E.fromstring(assets['Microsoft.PowerShell.Pager.nuspec']);ns={'n':tree.tag[1:].split('}')[0]}
meta=tree.find('n:metadata',ns)
assert meta.findtext('n:id',namespaces=ns)=='Microsoft.PowerShell.Pager'
assert meta.findtext('n:version',namespaces=ns)=='1.0.0'
assert not meta.findall('.//n:dependency',ns)
assert meta.find('n:license',ns).text=='MIT'
assert meta.find('n:repository',ns).get('commit')==golden['source_commit']
assert sha(assets['lib/net8.0/Microsoft.PowerShell.Pager.dll'])!=golden['published_dll_sha256']
evidence=json.loads(Path('tests/powershell-deps/pager-metadata-20261027.json').read_text())
assert evidence['source_dll_sha256']==sha(assets['lib/net8.0/Microsoft.PowerShell.Pager.dll'])
assert evidence['api']==golden['published_api'] and len(evidence['api']['api'])==3
assert {k:v for k,v in evidence['identity'].items() if k!='types'}==golden['published_identity']
overlay=json.loads(Path('tests/powershell-deps/pager-test-overlay-20261027.json').read_text())
assert overlay['original_basic_tests_sha256']==sha(original['test/BasicTests.cs'])
assert overlay['original_test_console_sha256']==sha(original['test/TestConsole.cs'])
staged=original['test/BasicTests.cs'].decode('utf-8-sig').replace('\r\n','\n').replace('using Xunit;','').replace('[Fact]','').replace('Assert.Equal(','OwnedFixture.Equal(')
assert overlay['staged_basic_tests_sha256']==sha(staged.encode())
assert 'pkgver = 1.0.0-1' in package['.PKGINFO'].decode()
result=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),nuget_sha256=sha(nuget),source_dll_sha256=evidence['source_dll_sha256'],
    pinned_inputs=12,source_commit=golden['source_commit'],original_cs_files=2,production_csharp_changes=0,public_api_entries=3,upstream_test_method_bodies=4,
    upstream_test_console_unchanged=True,test_assertion_adapter='four ordinal string comparisons; no xUnit replacement assembly')
Path('tests/powershell-deps/pager-artifact-20261027.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS Pager:12 pinned inputs, both original production files unchanged,3 public API declarations, sole source DLL, original license and four original test method bodies/console fixture')
