"""Audit original-source inventory, licenses, metadata and exact VB Workspaces payload."""
from pathlib import Path
import hashlib,io,json,subprocess,sys,tarfile,zipfile
import xml.etree.ElementTree as E

root=Path('mingw-w64-dotnet-roslyn-visualbasic-workspaces');fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
 key,sep,value=line.strip().partition(' = ')
 if sep:fields.setdefault(key,[]).append(value)
assert fields['pkgname']==['mingw-w64-clang-x86_64-dotnet-codeanalysis-visualbasic-workspaces']
assert fields['pkgver']==['4.9.2'] and fields['pkgrel']==['1']
assert len(fields['source'])==len(fields['sha256sums'])==14
source_sha='e01ea67aaec6408de51a0a4c18a7e15b2a527ca7a853c9a0a7742cb5680aeb30'
commit='9934fb9e3527e1c0c51314e57d4aab30f97e8f9e'
assert fields['sha256sums'][0]==source_sha
for name,wanted in zip(fields['source'],fields['sha256sums']):
 assert wanted!='SKIP'
 if '::' not in name:assert hashlib.sha256((root/name).read_bytes()).hexdigest()==wanted,name
expected_api=json.loads((root/'ExpectedApi.json').read_text());assert expected_api['api']==[]
assert expected_api['identity']=='Microsoft.CodeAnalysis.VisualBasic.Workspaces, Version=4.9.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35'
expected_services=json.loads((root/'ExpectedServices.json').read_text());assert len(expected_services)==48
from collections import Counter
assert Counter(k.split('.')[-1] for value in expected_services.values() for k in value)=={'ExportLanguageServiceAttribute':33,'ExportLanguageServiceFactoryAttribute':8,'ExportCodeCleanupProvider':6,'EditorConfigGeneratorAttribute':1}
metadata=json.loads((root/'ExpectedMetadata.json').read_text());assert len(metadata['resources'])==3 and sum(len(r) for r in metadata['resources'].values())==54
artifact=root/'mingw-w64-clang-x86_64-dotnet-codeanalysis-visualbasic-workspaces-4.9.2-1-any.pkg.tar.zst'
assert list(root.glob('*.pkg.tar.zst'))==[artifact]
entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
info=payload('.PKGINFO').decode();assert 'pkgver = 4.9.2-1\n' in info
suffixes=['dotnet-runtime=8.0.131','dotnet-codeanalysis-common=4.9.2','dotnet-codeanalysis-visualbasic=4.9.2','dotnet-codeanalysis-workspaces-common=4.9.2']
assert sorted(line[9:] for line in info.splitlines() if line.startswith('depend = '))==sorted('mingw-w64-clang-x86_64-'+s for s in suffixes)
licenses={'License.txt':'ae48df11a335dc1a615f4f938b69cba73bcf4485c4f97af49b38efb0f216353b','THIRD-PARTY-NOTICES.txt':'aa10edcdc488e201fdca979306ba0617de4c68c58dfa5916b25e588f5369fb82'}
for filename,sha in licenses.items():assert hashlib.sha256(payload('clang64/share/licenses/dotnet-codeanalysis-visualbasic-workspaces/'+filename)).hexdigest()==sha
prefix='clang64/share/doc/dotnet-codeanalysis-visualbasic-workspaces/'
for filename in ['README.md','ExpectedApi.json','ExpectedMetadata.json','ExpectedServices.json','published-metadata-provenance.json']:assert payload(prefix+filename)==(root/filename).read_bytes(),filename
sources=json.loads(payload(prefix+'source-items.json'));inventory=sources['Microsoft.CodeAnalysis.VisualBasic.Workspaces']
assert inventory['source_files']==len(inventory['source_paths'])==len(inventory['source_sha256'])==234
assert len(inventory['imports'])==3 and len(inventory['friends'])==19
assert len(inventory['resources'])==3
# Verify every production source hash independently against the pinned original archive.
archive=Path('/tmp/powershell-roslyn/roslyn-4.9.2.tar.gz')
if not archive.exists():archive=root/'roslyn-4.9.2.tar.gz'
assert archive.exists(),'Pinned archive required for independent original-source verification'
assert hashlib.sha256(archive.read_bytes()).hexdigest()==source_sha
with tarfile.open(archive) as t:
 base='roslyn-'+commit+'/'
 original={p.removeprefix(base) for p in t.getnames() if p.startswith(base+'src/Workspaces/VisualBasic/Portable/') and p.endswith('.vb') and '/My Project/' not in p}
 visited=set()
 def collect(path):
  if path in visited:return
  visited.add(path);tree=E.fromstring(t.extractfile(base+path).read());parent=Path(path).parent
  for e in tree.iter():
   kind=e.tag.rsplit('}',1)[-1]
   if kind=='Import' and e.get('Project'):
    target=e.get('Project').replace('\\','/');assert '$(' not in target
    import posixpath
    collect(posixpath.normpath(str(parent/target)))
   elif kind=='Compile' and e.get('Include'):
    target=e.get('Include').replace('$(MSBuildThisFileDirectory)',str(parent)+'/').replace('\\','/')
    assert '$(' not in target and '*' not in target;original.add(target)
 collect('src/Workspaces/VisualBasic/Portable/Microsoft.CodeAnalysis.VisualBasic.Workspaces.vbproj')
 assert set(inventory['source_paths'])==original and set(inventory['imports'])==visited
 for path,sha in inventory['source_sha256'].items():assert hashlib.sha256(t.extractfile(base+path).read()).hexdigest()==sha,path
localizations=json.loads(payload(prefix+'localization.json'));assert len(localizations)==42 and sum(len(r['strings']) for r in localizations)==756
cultures=['cs','de','es','fr','it','ja','ko','pl','pt-BR','ru','tr','zh-Hans','zh-Hant']
assert sorted({r['culture'] for r in localizations})==sorted(['']+cultures)
name='Microsoft.CodeAnalysis.VisualBasic.Workspaces';target='clang64/lib/dotnet/library-packs/microsoft.codeanalysis.visualbasic.workspaces.4.9.2.nupkg'
assert [p for p in entries if p.endswith('.nupkg')]==[target]
with zipfile.ZipFile(io.BytesIO(payload(target))) as z:
 spec=E.fromstring(z.read(name+'.nuspec'))
 assert spec.findtext('.//{*}id')==name and spec.findtext('.//{*}version')=='4.9.2'
 assert spec.findtext('.//{*}license')=='MIT' and spec.find('.//{*}repository').get('commit')==commit
 assert z.read('README.md')==(root/'README.md').read_bytes()
 assert not any(p.endswith(('.pdb','.snk','.p7s')) for p in z.namelist())
 deps={e.get('id'):e.get('version') for e in spec.findall('.//{*}dependency')}
 assert deps=={n:'[4.9.2]' for n in ['Microsoft.CodeAnalysis.Common','Microsoft.CodeAnalysis.VisualBasic','Microsoft.CodeAnalysis.Workspaces.Common']},deps
 dlls=sorted(p for p in z.namelist() if p.endswith('.dll'))
 assert dlls==sorted(['lib/net8.0/'+name+'.dll']+['lib/net8.0/'+c+'/'+name+'.resources.dll' for c in cultures]),dlls
 hashes={p:hashlib.sha256(z.read(p)).hexdigest() for p in dlls}
record=dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),nuget_id=name,nuget_version='4.9.2',nuget_dependencies=deps,source_commit=commit,source_sha256=source_sha,managed_payload_sha256=hashes,public_api_declarations=0,mef_export_types=48,language_services=33,language_service_factories=8,cleanup_providers=6,editorconfig_generators=1,original_source_files=234,independent_source_archive_hashes_match=True,resource_sets=42,resource_strings=756,assembly_version='4.9.0.0',public_key_token='31bf3856ad364e35')
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(record,indent=2)+'\n')
print('Roslyn VB Workspaces: pinned inputs/licenses/dependencies, independent 234-file original-source inventory, 48 MEF export baseline, 42/756 resources and exact fourteen DLL payloads PASS')
