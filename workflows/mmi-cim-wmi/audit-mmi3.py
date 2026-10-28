"""Audit the experimental MMI3 contract port without claiming full compatibility."""
import hashlib,io,json,subprocess,sys,zipfile
from pathlib import Path
import xml.etree.ElementTree as E
root=Path('mingw-w64-dotnet-mmi3');fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
 key,sep,value=line.strip().partition(' = ')
 if sep:fields.setdefault(key,[]).append(value)
assert fields['pkgver']==['3.0.0'] and fields['pkgrel']==['1']
assert len(fields['source'])==len(fields['sha256sums'])
for source,wanted in zip(fields['source'],fields['sha256sums']):
 assert wanted!='SKIP'
 if '::' not in source:assert hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
artifacts=list(root.glob('*.pkg.tar.zst'));assert len(artifacts)==1
artifact=artifacts[0]
entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
info=payload('.PKGINFO').decode();assert 'pkgver = 3.0.0-1\n' in info
assert [s[9:] for s in info.splitlines() if s.startswith('depend = ')]==['mingw-w64-clang-x86_64-dotnet-runtime=8.0.131']
assert 'clang64/share/licenses/dotnet-mmi3/LICENSE' in entries
assert 'clang64/share/doc/dotnet-mmi3/README.md' in entries
api=json.loads(payload('clang64/share/doc/dotnet-mmi3/ExpectedApi.json'))
assert len(api['api'])==512 and len(api['allowed_extra'])==3
assert api['identity']=='Microsoft.Management.Infrastructure, Version=1.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35'
name='clang64/lib/dotnet/library-packs/microsoft.management.infrastructure.3.0.0.nupkg'
assert [p for p in entries if p.endswith('.nupkg')]==[name]
with zipfile.ZipFile(io.BytesIO(payload(name))) as z:
 spec=E.fromstring(z.read('Microsoft.Management.Infrastructure.nuspec'))
 assert spec.findtext('.//{*}id')=='Microsoft.Management.Infrastructure' and spec.findtext('.//{*}version')=='3.0.0'
 assert not spec.findall('.//{*}dependency')
 assert [p for p in z.namelist() if p.endswith('.dll')]==['lib/net8.0/Microsoft.Management.Infrastructure.dll']
 dll_hash=hashlib.sha256(z.read('lib/net8.0/Microsoft.Management.Infrastructure.dll')).hexdigest()
record=dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),nuget_id='Microsoft.Management.Infrastructure',nuget_version='3.0.0',assembly_version='1.0.0.0',public_key_token='31bf3856ad364e35',actual_source_commit='cf32fc695b32c2eae848b693395c09222fe1d2ae',actual_source_version='2.0.0-preview.1 plus seven native patches and explicit MMI3 contract corrections',source_sha256=fields['sha256sums'][0],managed_dll_sha256=dll_hash,expected_api_entries=512,permitted_cleanup_finalizers=3,status='experimental contract/native-local port; full PowerShell/remote/ownership compatibility incomplete')
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(record,indent=2)+'\n')
print('MMI3 candidate: exact pinned inputs, identity/version/dependencies, 512-entry contract metadata, license and sole managed payload PASS; full compatibility remains open')
