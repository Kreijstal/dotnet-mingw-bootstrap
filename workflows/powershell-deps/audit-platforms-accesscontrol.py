"""Audit exact payloads and pinned inputs for the graph and ACL6 source ports."""
import hashlib,io,json,subprocess,sys,zipfile
from pathlib import Path
import xml.etree.ElementTree as E
records=[]
for suffix,name,version in [('netcore-platforms','Microsoft.NETCore.Platforms','1.1.2'),('security-accesscontrol6','System.Security.AccessControl','6.0.1')]:
 root=Path('mingw-w64-dotnet-'+suffix);fields={}
 for line in (root/'.SRCINFO').read_text().splitlines():
  key,sep,value=line.strip().partition(' = ')
  if sep:fields.setdefault(key,[]).append(value)
 assert len(fields['source'])==len(fields['sha256sums'])
 for source,wanted in zip(fields['source'],fields['sha256sums']):
  assert wanted!='SKIP'
  if '::' not in source:assert hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
 artifacts=list(root.glob('*.pkg.tar.zst'));assert len(artifacts)==1
 artifact=artifacts[0]
 entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
 def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
 prefix='clang64/share/'
 for path in ['licenses/dotnet-'+suffix+'/LICENSE','licenses/dotnet-'+suffix+'/THIRD-PARTY-NOTICES','doc/dotnet-'+suffix+'/README.md']:assert prefix+path in entries
 info=payload('.PKGINFO').decode();assert 'pkgver = '+version+'-1\n' in info
 deps=sorted(line[9:] for line in info.splitlines() if line.startswith('depend = '))
 assert deps==([] if suffix=='netcore-platforms' else ['mingw-w64-clang-x86_64-dotnet-runtime=8.0.131'])
 nupkg='clang64/lib/dotnet/library-packs/'+name.lower()+'.'+version+'.nupkg'
 assert [p for p in entries if p.endswith('.nupkg')]==[nupkg]
 with zipfile.ZipFile(io.BytesIO(payload(nupkg))) as z:
  spec=E.fromstring(z.read(next(p for p in z.namelist() if p.endswith('.nuspec'))))
  assert spec.findtext('.//{*}id')==name and spec.findtext('.//{*}version')==version
  assert not spec.findall('.//{*}dependency')
  repository=spec.find('.//{*}repository');assert repository is not None
  dlls=sorted(p for p in z.namelist() if p.endswith('.dll'))
  checks={}
  if suffix=='netcore-platforms':
   assert not dlls
   data=z.read('runtime.json');graph=json.loads(data)['runtimes']
   assert hashlib.sha256(data).hexdigest()=='2738181f98f68ee46bb401ff9ded2f7eec03e1d517004337607dcbdfdb1b99f1'
   assert len(graph)==165 and z.read('lib/netstandard1.0/_._')==b''
   assert repository.get('commit')=='718443a10dbd456b6c8f08ee594a92cfbf2ef8db'
   checks={'runtime_ids':165,'runtime_json_sha256':hashlib.sha256(data).hexdigest()}
  else:
   assert dlls==['ref/net8.0/System.Security.AccessControl.dll','runtimes/win/lib/net8.0/System.Security.AccessControl.dll']
   assert repository.get('commit')=='189fbbd88d97dd6d65515ba2da05b62eab4e5039'
   localized=json.loads(payload(prefix+'doc/dotnet-'+suffix+'/localization.json'))
   assert len(localized)==1 and len(localized[0]['strings'])==47
   assert isinstance(json.loads(payload(prefix+'doc/dotnet-'+suffix+'/removed-binaries.json')),list)
   checks={'managed_dlls':{p:hashlib.sha256(z.read(p)).hexdigest() for p in dlls},'neutral_resources':47}
 records.append(dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),nuget_id=name,nuget_version=version,source_sha256=fields['sha256sums'][0],checks=checks))
 print(name+': exact source checksums, package identity/dependencies, licenses and payload PASS')
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(records,indent=2)+'\n')
