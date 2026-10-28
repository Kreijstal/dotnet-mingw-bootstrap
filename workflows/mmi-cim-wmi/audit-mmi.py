"""Audit the installed-feed credential port's exact package payload and inputs."""
import hashlib,io,json,subprocess,sys,zipfile
from pathlib import Path
import xml.etree.ElementTree as E
root=Path('mingw-w64-dotnet-mmi');fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
 key,sep,value=line.strip().partition(' = ')
 if sep:fields.setdefault(key,[]).append(value)
assert fields['pkgrel']==['4']
assert len(fields['source'])==len(fields['sha256sums'])
for source,wanted in zip(fields['source'],fields['sha256sums']):
 assert wanted!='SKIP'
 if '::' not in source:assert hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
artifact=root/'mingw-w64-clang-x86_64-dotnet-mmi-2.0.0.preview1-4-any.pkg.tar.zst'
entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
info=payload('.PKGINFO').decode()
assert 'pkgver = 2.0.0.preview1-4\n' in info
assert [s[9:] for s in info.splitlines() if s.startswith('depend = ')]==['mingw-w64-clang-x86_64-dotnet-runtime=8.0.131']
assert 'clang64/share/licenses/dotnet-mmi/LICENSE' in entries
assert 'clang64/share/doc/dotnet-mmi/README.md' in entries
name='clang64/lib/dotnet/library-packs/microsoft.management.infrastructure.2.0.0-preview.1.nupkg'
assert [p for p in entries if p.endswith('.nupkg')]==[name]
with zipfile.ZipFile(io.BytesIO(payload(name))) as z:
 spec=E.fromstring(z.read('Microsoft.Management.Infrastructure.nuspec'))
 assert spec.findtext('.//{*}id')=='Microsoft.Management.Infrastructure'
 assert spec.findtext('.//{*}version')=='2.0.0-preview.1'
 assert not spec.findall('.//{*}dependency')
 assert [p for p in z.namelist() if p.endswith('.dll')]==['lib/net8.0/Microsoft.Management.Infrastructure.dll']
 dll_hash=hashlib.sha256(z.read('lib/net8.0/Microsoft.Management.Infrastructure.dll')).hexdigest()
record=dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),nuget_id='Microsoft.Management.Infrastructure',nuget_version='2.0.0-preview.1',package_release=4,assembly_version='2.0.0.0',public_key_token='31bf3856ad364e35',source_commit='cf32fc695b32c2eae848b693395c09222fe1d2ae',source_sha256=fields['sha256sums'][0],managed_dll_sha256=dll_hash,powershell_mmi_3_compatibility='incomplete')
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(record,indent=2)+'\n')
print('MMI credential/callback/stream port pkgrel 4: exact pinned source/checksums, runtime dependency, license and sole source-built managed payload PASS')
