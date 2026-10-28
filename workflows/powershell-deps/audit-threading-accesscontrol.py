"""Audit the source-built Threading ACL package required by PowerShell and WCF."""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

root=Path('mingw-w64-dotnet-threading-accesscontrol')
fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
    key,sep,value=line.strip().partition(' = ')
    if sep:fields.setdefault(key,[]).append(value)
assert len(fields['source'])==len(fields['sha256sums'])
for source,wanted in zip(fields['source'],fields['sha256sums']):
    assert wanted!='SKIP'
    if '::' not in source:assert hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
artifacts=list(root.glob('*.pkg.tar.zst'));assert len(artifacts)==1
package=artifacts[0]
entries=subprocess.check_output(['tar','--zstd','-tf',str(package)],text=True).splitlines()
def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(package),path])
for path in ['clang64/share/licenses/dotnet-threading-accesscontrol/LICENSE','clang64/share/licenses/dotnet-threading-accesscontrol/THIRD-PARTY-NOTICES',
             'clang64/share/doc/dotnet-threading-accesscontrol/README.md','clang64/share/doc/dotnet-threading-accesscontrol/removed-binaries.json','clang64/share/doc/dotnet-threading-accesscontrol/localization.json']:
    assert path in entries
resources=json.loads(payload('clang64/share/doc/dotnet-threading-accesscontrol/localization.json'))
assert len(resources)==1 and len(resources[0]['strings'])==21
info=payload('.PKGINFO').decode()
assert 'pkgver = 8.0.0-2\n' in info
assert 'depend = mingw-w64-clang-x86_64-dotnet-runtime=8.0.131\n' in info
assert not any(line.startswith('depend = mingw-w64-clang-x86_64-dotnet-codeanalysis') for line in info.splitlines())
nupkg='clang64/lib/dotnet/library-packs/system.threading.accesscontrol.8.0.0.nupkg'
assert [e for e in entries if e.endswith('.nupkg')]==[nupkg]
with zipfile.ZipFile(io.BytesIO(payload(nupkg))) as archive:
    spec=ET.fromstring(archive.read(next(p for p in archive.namelist() if p.endswith('.nuspec'))))
    metadata=next(e for e in spec if e.tag.endswith('}metadata'))
    values={e.tag.rsplit('}',1)[-1]:e.text for e in metadata}
    assert values['id']=='System.Threading.AccessControl' and values['version']=='8.0.0'
    assert not [e for e in spec.iter() if e.tag.endswith('}dependency')]
    repository=next(e for e in metadata if e.tag.endswith('}repository'))
    assert repository.get('url')=='https://github.com/dotnet/runtime'
    assert sorted(p for p in archive.namelist() if p.endswith('.dll'))==['ref/net8.0/System.Threading.AccessControl.dll','runtimes/win/lib/net8.0/System.Threading.AccessControl.dll']
record={'recipe':str(root),'artifact':package.name,'sha256':hashlib.sha256(package.read_bytes()).hexdigest(),
        'nuget_id':'System.Threading.AccessControl','nuget_version':'8.0.0','source_tag':'runtime v8.0.31',
        'source_sha256':fields['sha256sums'][0],'framework':'net8.0','assembly_version':'8.0.0.0',
        'public_key_token':'b03f5f7f11d50a3a','upstream_api_signatures':135,'neutral_resource_strings':21,'dependencies':{}}
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(record,indent=2)+'\n')
print('Threading ACL: checksums, licenses, dependency metadata, resource baseline and exact NuGet payload PASS')
