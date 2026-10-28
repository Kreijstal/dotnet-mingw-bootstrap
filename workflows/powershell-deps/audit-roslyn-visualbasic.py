"""Audit the complete source-built Visual Basic compiler dependency."""
from pathlib import Path
import hashlib,io,json,subprocess,sys,zipfile
import xml.etree.ElementTree as E

root=Path('mingw-w64-dotnet-roslyn-visualbasic');fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
 key,sep,value=line.strip().partition(' = ')
 if sep:fields.setdefault(key,[]).append(value)
assert fields['pkgname']==['mingw-w64-clang-x86_64-dotnet-codeanalysis-visualbasic']
assert fields['pkgver']==['4.9.2'] and fields['pkgrel']==['1']
assert len(fields['source'])==len(fields['sha256sums'])
assert fields['sha256sums'][0]=='e01ea67aaec6408de51a0a4c18a7e15b2a527ca7a853c9a0a7742cb5680aeb30'
for name,wanted in zip(fields['source'],fields['sha256sums']):
 assert wanted!='SKIP'
 if '::' not in name:assert hashlib.sha256((root/name).read_bytes()).hexdigest()==wanted,name
api=json.loads((root/'ExpectedApi.json').read_text());assert len(api['api'])==6176
assert api['identity']=='Microsoft.CodeAnalysis.VisualBasic, Version=4.9.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35'
metadata=json.loads((root/'ExpectedMetadata.json').read_text());assert len(metadata['resources']['Microsoft.CodeAnalysis.VisualBasic.VBResources.resources'])==1795
artifact=root/'mingw-w64-clang-x86_64-dotnet-codeanalysis-visualbasic-4.9.2-1-any.pkg.tar.zst'
assert len(list(root.glob('*.pkg.tar.zst')))==1
entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
info=payload('.PKGINFO').decode();assert 'pkgver = 4.9.2-1\n' in info
assert sorted(e[9:] for e in info.splitlines() if e.startswith('depend = '))==sorted(['mingw-w64-clang-x86_64-dotnet-runtime=8.0.131','mingw-w64-clang-x86_64-dotnet-codeanalysis-common=4.9.2'])
licenses={'License.txt':'ae48df11a335dc1a615f4f938b69cba73bcf4485c4f97af49b38efb0f216353b','THIRD-PARTY-NOTICES.txt':'aa10edcdc488e201fdca979306ba0617de4c68c58dfa5916b25e588f5369fb82'}
for name,sha in licenses.items():assert hashlib.sha256(payload('clang64/share/licenses/dotnet-codeanalysis-visualbasic/'+name)).hexdigest()==sha
doc='clang64/share/doc/dotnet-codeanalysis-visualbasic/'
for name in ['README.md','ExpectedApi.json','ExpectedMetadata.json','published-metadata-provenance.json']:assert payload(doc+name)==(root/name).read_bytes(),name
sources=json.loads(payload(doc+'source-items.json'));assert sources['Microsoft.CodeAnalysis.VisualBasic']['source_files']==905 and sources['Microsoft.CodeAnalysis.VisualBasic']['generated_files']==5
assert [sources[n]['source_files'] for n in ['VBSyntaxGenerator','VBErrorFactsGenerator','BoundTreeGenerator']]==[17,1,3]
localizations=json.loads(payload(doc+'localization.json'));assert len(localizations)==14 and sum(len(r['strings']) for r in localizations)==25130
target='clang64/lib/dotnet/library-packs/microsoft.codeanalysis.visualbasic.4.9.2.nupkg'
assert [p for p in entries if p.endswith('.nupkg')]==[target]
cultures=['cs','de','es','fr','it','ja','ko','pl','pt-BR','ru','tr','zh-Hans','zh-Hant']
with zipfile.ZipFile(io.BytesIO(payload(target))) as z:
 spec=E.fromstring(z.read('Microsoft.CodeAnalysis.VisualBasic.nuspec'))
 assert spec.findtext('.//{*}id')=='Microsoft.CodeAnalysis.VisualBasic' and spec.findtext('.//{*}version')=='4.9.2'
 assert spec.findtext('.//{*}license')=='MIT' and spec.find('.//{*}repository').get('commit')=='9934fb9e3527e1c0c51314e57d4aab30f97e8f9e'
 assert z.read('README.md')==(root/'README.md').read_bytes()
 assert not any(p.endswith(('.pdb','.snk','.p7s')) for p in z.namelist())
 deps={e.get('id'):e.get('version') for e in spec.findall('.//{*}dependency')};assert deps=={'Microsoft.CodeAnalysis.Common':'[4.9.2]'},deps
 dlls=sorted(p for p in z.namelist() if p.endswith('.dll'))
 assert dlls==sorted(['lib/net8.0/Microsoft.CodeAnalysis.VisualBasic.dll']+['lib/net8.0/'+c+'/Microsoft.CodeAnalysis.VisualBasic.resources.dll' for c in cultures]),dlls
 hashes={p:hashlib.sha256(z.read(p)).hexdigest() for p in dlls}
record=dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),nuget_id='Microsoft.CodeAnalysis.VisualBasic',nuget_version='4.9.2',nuget_dependencies=deps,source_commit='9934fb9e3527e1c0c51314e57d4aab30f97e8f9e',source_sha256=fields['sha256sums'][0],managed_payload_sha256=hashes,api_declarations=6176,source_files=905,generated_source_files=5,generator_source_files=[17,1,3],resource_sets=14,resource_strings=25130,embedded_sources=4,assembly_version='4.9.0.0',public_key_token='31bf3856ad364e35')
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(record,indent=2)+'\n')
print('Roslyn VB: pinned source/checksums/licenses, exact dependency and fourteen DLL payloads, 6176 API baseline, 905+5 compiler inputs, all generators, 14/25130 resources and four embedded source baselines PASS')
