"""Audit the source-built MEF2/Pipelines layer required by Roslyn Workspaces."""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as E
import zipfile

root=Path('mingw-w64-dotnet-workspaces-prerequisites')
fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
 key,sep,value=line.strip().partition(' = ')
 if sep:fields.setdefault(key,[]).append(value)
assert fields['pkgver']==['8.0.31'] and fields['pkgrel']==['1']
assert len(fields['source'])==len(fields['sha256sums'])
assert fields['sha256sums'][0]=='b32a56a29cc0248bed219e1c7ef026bf8f006d997f5d6346382e63a22ecd9070'
for source,wanted in zip(fields['source'],fields['sha256sums']):
 assert wanted!='SKIP'
 if '::' not in source:assert hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
libraries=json.loads((root/'libraries.json').read_text())
expected_api=json.loads((root/'ExpectedApi.json').read_text())
assert sum(len(lib['api']) for lib in expected_api.values())==317
libraries.append(dict(suffix='composition2',id='System.Composition',dependencies={lib['id']:'8.0.0' for lib in libraries if lib['id'].startswith('System.Composition.')}))
assert len(list(root.glob('*.pkg.tar.zst')))==7
records=[]
for lib in libraries:
 name,suffix=lib['id'],lib['suffix']
 artifact=root/('mingw-w64-clang-x86_64-dotnet-'+suffix+'-8.0.31-1-any.pkg.tar.zst')
 entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
 def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
 info=payload('.PKGINFO').decode()
 assert 'pkgver = 8.0.31-1\n' in info
 deps=['mingw-w64-clang-x86_64-dotnet-runtime=8.0.131']+['mingw-w64-clang-x86_64-dotnet-'+next(v['suffix'] for v in libraries if v['id']==dep)+'=8.0.31' for dep in lib['dependencies']]
 assert sorted(line[9:] for line in info.splitlines() if line.startswith('depend = '))==sorted(deps),(name,deps)
 for file in ['LICENSE','THIRD-PARTY-NOTICES']:assert 'clang64/share/licenses/dotnet-'+suffix+'/'+file in entries
 for file in ['README.md','ExpectedApi.json','libraries.json','published-metadata-provenance.json']:
  assert payload('clang64/share/doc/dotnet-'+suffix+'/'+file)==(root/file).read_bytes()
 resources=json.loads(payload('clang64/share/doc/dotnet-'+suffix+'/localization.json'))
 assert len(resources)==5 and sum(len(e['strings']) for e in resources)==70
 target='clang64/lib/dotnet/library-packs/'+name.lower()+'.8.0.0.nupkg'
 assert [p for p in entries if p.endswith('.nupkg')]==[target]
 with zipfile.ZipFile(io.BytesIO(payload(target))) as z:
  spec=E.fromstring(z.read(name+'.nuspec'))
  assert spec.findtext('.//{*}id')==name and spec.findtext('.//{*}version')=='8.0.0'
  assert spec.findtext('.//{*}license')=='MIT'
  assert {e.get('id'):e.get('version') for e in spec.findall('.//{*}dependency')}=={dep:'['+version+']' for dep,version in lib['dependencies'].items()},name
  assert z.read('README.md')==(root/'README.md').read_bytes()
  dlls=[p for p in z.namelist() if p.endswith('.dll')]
  if name=='System.Composition':
   assert not dlls and z.read('lib/net8.0/_._')==b''
   dll_hash=None
  else:
   assert dlls==['lib/net8.0/'+name+'.dll'],(name,dlls)
   dll_hash=hashlib.sha256(z.read(dlls[0])).hexdigest()
 record=dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),nuget_id=name,nuget_version='8.0.0',source_tag='runtime v8.0.31',source_sha256=fields['sha256sums'][0],dependencies=lib['dependencies'],managed_sha256=dll_hash)
 if name in expected_api:record.update(identity=expected_api[name]['identity'],published_api_declarations=len(expected_api[name]['api']))
 records.append(record)
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(records,indent=2)+'\n')
print('MEF2/Pipelines: all seven source-built package payloads, exact pinned inputs, licenses, dependency graphs, net8.0 meta placeholder, 317-entry API baseline and five/70 resource sets/strings PASS')
