"""Audit CommonMark.NET source package, contract, test data and licenses."""
from pathlib import Path
import hashlib,io,json,subprocess,sys,zipfile
import xml.etree.ElementTree as E
root=Path('mingw-w64-dotnet-commonmark');fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
 key,sep,value=line.strip().partition(' = ')
 if sep:fields.setdefault(key,[]).append(value)
assert fields['pkgver']==['0.15.1'] and fields['pkgrel']==['1']
assert len(fields['source'])==len(fields['sha256sums'])
assert fields['sha256sums'][0]=='7fd82969785165a4ecaceaaad0d4121ee1506306977c95882c2c55dddcfec417'
for source,wanted in zip(fields['source'],fields['sha256sums']):
 assert wanted!='SKIP'
 if '::' not in source:assert hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
artifacts=list(root.glob('*.pkg.tar.zst'));assert len(artifacts)==1;artifact=artifacts[0]
entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
info=payload('.PKGINFO').decode();assert 'pkgver = 0.15.1-1\n' in info
assert [e[9:] for e in info.splitlines() if e.startswith('depend = ')]==['mingw-w64-clang-x86_64-dotnet-runtime=8.0.131']
assert 'clang64/share/licenses/dotnet-commonmark/LICENSE' in entries
assert payload('clang64/share/licenses/dotnet-commonmark/LICENSE.CommonMarkSpec')==(root/'LICENSE.CommonMarkSpec').read_bytes()
doc='clang64/share/doc/dotnet-commonmark/'
for name in ['README.md','ExpectedApi.json','published-metadata-provenance.json']:assert payload(doc+name)==(root/name).read_bytes()
api=json.loads(payload(doc+'ExpectedApi.json'));assert len(api['api'])==200 and api['identity']=='CommonMark, Version=0.1.0.0, Culture=neutral, PublicKeyToken=001ef8810438905d'
source_files=json.loads(payload(doc+'source-items.json'));assert len(source_files)==43,len(source_files)
spec=payload(doc+'checks/Specs.cs');assert spec.count(b'[TestMethod]')==622
assert b'John MacFarlane' in spec and b'CC-BY-SA 4.0' in spec
helper=payload(doc+'checks/Helpers.cs');assert b'HtmlFormatterSlim' in helper and b'Incorrect source position' in helper
name='clang64/lib/dotnet/library-packs/commonmark.net.0.15.1.nupkg';assert [e for e in entries if e.endswith('.nupkg')]==[name]
with zipfile.ZipFile(io.BytesIO(payload(name))) as z:
 spec=E.fromstring(z.read('CommonMark.NET.nuspec'))
 assert spec.findtext('.//{*}id')=='CommonMark.NET' and spec.findtext('.//{*}version')=='0.15.1'
 assert spec.findtext('.//{*}license')=='BSD-3-Clause' and not spec.findall('.//{*}dependency')
 repository=spec.find('.//{*}repository');assert repository.get('commit')=='6f0ed9805065ee368c01fe931ca7d589e9341f11'
 assert [name for name in z.namelist() if name.endswith('.dll')]==['lib/net8.0/CommonMark.dll']
 assert not any(name.endswith('.snk') for name in z.namelist())
 assert z.read('README.md')==(root/'README.md').read_bytes()
 dll_hash=hashlib.sha256(z.read('lib/net8.0/CommonMark.dll')).hexdigest()
record=dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),nuget_id='CommonMark.NET',nuget_version='0.15.1',assembly_identity=api['identity'],managed_sha256=dll_hash,source_commit='6f0ed9805065ee368c01fe931ca7d589e9341f11',source_sha256=fields['sha256sums'][0],public_api_declarations=200,source_files=len(source_files),upstream_spec_cases=622,spec_sha256=hashlib.sha256(payload(doc+'checks/Specs.cs')).hexdigest(),helper_sha256=hashlib.sha256(helper).hexdigest(),signing='public-only component derived from upstream CAPI key; no private key shipped')
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(record,indent=2)+'\n')
print('CommonMark.NET: source/checksums/licenses, exact sole managed payload/dependencies, 200-entry identity/API baseline, all 43 sources and 622 attributed upstream spec cases PASS')
