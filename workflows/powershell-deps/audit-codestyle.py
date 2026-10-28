"""Audit complete CodeStyle packages, SDK integration and independent original source coverage."""
from pathlib import Path
import hashlib,io,json,posixpath,subprocess,sys,tarfile,zipfile
import xml.etree.ElementTree as E
r=Path('mingw-w64-dotnet-codestyle');fields={}
for line in (r/'.SRCINFO').read_text().splitlines():
 key,sep,value=line.strip().partition(' = ')
 if sep:fields.setdefault(key,[]).append(value)
assert fields['pkgver']==['4.9.2'] and fields['pkgrel']==['1']
assert fields['pkgname']==['mingw-w64-clang-x86_64-dotnet-codestyle-csharp','mingw-w64-clang-x86_64-dotnet-codestyle-visualbasic','mingw-w64-clang-x86_64-dotnet-sdk-codestyle']
assert len(fields['source'])==len(fields['sha256sums'])==19
source_sha='e01ea67aaec6408de51a0a4c18a7e15b2a527ca7a853c9a0a7742cb5680aeb30';commit='9934fb9e3527e1c0c51314e57d4aab30f97e8f9e'
assert fields['sha256sums'][0]==source_sha
for name,sha in zip(fields['source'],fields['sha256sums']):
 assert sha!='SKIP'
 if '::' not in name:assert hashlib.sha256((r/name).read_bytes()).hexdigest()==sha,name
archive=Path('/tmp/powershell-roslyn/roslyn-4.9.2.tar.gz')
if not archive.exists():archive=r/'roslyn-4.9.2.tar.gz'
assert hashlib.sha256(archive.read_bytes()).hexdigest()==source_sha
with tarfile.open(archive) as t:
 prefix='roslyn-'+commit+'/'
 original={m.name.removeprefix(prefix):t.extractfile(m).read() for m in t.getmembers() if m.isfile() and m.name.startswith(prefix)}
expected=json.loads((r/'ExpectedAssemblies.json').read_text());build_assets=json.loads((r/'ExpectedBuildAssets.json').read_text())
assert sum(len(v['api']['api']) for v in expected.values())==100
assert sum(len(v['inventory']['analyzers']) for v in expected.values())==135
assert sum(len(v['inventory']['code_fixes']) for v in expected.values())==172
assert sum(len(v['inventory']['diagnostic_ids']) for v in expected.values())==138
fixtures=json.loads((r/'UpstreamFixtures.json').read_text());assert len(fixtures['cases'])==146
cultures=['cs','de','es','fr','it','ja','ko','pl','pt-BR','ru','tr','zh-Hans','zh-Hant'];records={};dlls={};source_counts=None
comments={frozenset({'# IDE0053: Use expression body for lambda expression','# IDE0053: Use block body for lambda expression'}),frozenset({'# IDE0073: The file header does not match the required text','# IDE0073: The file header is missing or not located at the top of the file'})}
def read_package(artifact):
 data=subprocess.check_output(['zstd','-dc',str(artifact)])
 with tarfile.open(fileobj=io.BytesIO(data)) as t:return {m.name:t.extractfile(m).read() for m in t.getmembers() if m.isfile()}
def check_text(actual,wanted):
 a=actual.decode('utf-8-sig').replace('\r\n','\n').splitlines();b=wanted.splitlines();assert len(a)==len(b)
 for x,y in zip(a,b):assert x==y or frozenset({x,y}) in comments,(x,y)
def resolve(parent,path):
 path=path.replace('$(MSBuildThisFileDirectory)',parent+'/').replace('\\','/')
 assert '$(' not in path and '*' not in path,path
 return posixpath.normpath(path if path.startswith('src/') else parent+'/'+path)
def tag(e):return e.tag.rsplit('}',1)[-1]
for language,short in [('CSharp','cs'),('VisualBasic','vb')]:
 suffix=language.lower();artifact=r/('mingw-w64-clang-x86_64-dotnet-codestyle-'+suffix+'-4.9.2-1-any.pkg.tar.zst');payload=read_package(artifact)
 info=payload['.PKGINFO'].decode();assert 'pkgver = 4.9.2-1\n' in info
 wanted=['dotnet-runtime=8.0.131','dotnet-codeanalysis-'+suffix+'-workspaces=4.9.2','dotnet-'+('csharp' if short=='cs' else 'visualbasic')+'-compiler=4.9.2']
 assert sorted(s[9:] for s in info.splitlines() if s.startswith('depend = '))==sorted('mingw-w64-clang-x86_64-'+s for s in wanted)
 doc='clang64/share/doc/dotnet-codestyle-'+suffix+'/'
 for name in ['README.md','ExpectedAssemblies.json','ExpectedBuildAssets.json','UpstreamFixtures.json','published-metadata-provenance.json']:assert payload[doc+name]==(r/name).read_bytes(),name
 license_root='clang64/share/licenses/dotnet-codestyle-'+suffix+'/'
 for name in ['License.txt','THIRD-PARTY-NOTICES.txt']:assert payload[license_root+name]==original[name]
 sources=json.loads(payload[doc+'source-items.json']);resources=json.loads(payload[doc+'localization.json'])
 assert len(resources)==210 and sum(len(e['strings']) for e in resources)==5684
 counts={name:entry['source_files'] for name,entry in sources.items()}
 assert list(counts.values())==[659,254,114,213,220,101,5],counts
 if source_counts is None:source_counts=counts
 else:assert source_counts==counts
 package_path='clang64/lib/dotnet/library-packs/microsoft.codeanalysis.'+suffix+'.codestyle.4.9.2.nupkg'
 assert [p for p in payload if p.endswith('.nupkg')]==[package_path]
 with zipfile.ZipFile(io.BytesIO(payload[package_path])) as z:
  spec=E.fromstring(z.read('Microsoft.CodeAnalysis.'+language+'.CodeStyle.nuspec'))
  assert spec.findtext('.//{*}id')=='Microsoft.CodeAnalysis.'+language+'.CodeStyle'
  assert spec.findtext('.//{*}version')=='4.9.2' and spec.findtext('.//{*}developmentDependency')=='true'
  assert spec.findtext('.//{*}license')=='MIT' and spec.find('.//{*}repository').get('commit')==commit
  assert not spec.findall('.//{*}dependency')
  assert not any(p.endswith(('.snk','.pdb','.p7s','.nupkg')) for p in z.namelist())
  names=['Microsoft.CodeAnalysis.CodeStyle','Microsoft.CodeAnalysis.CodeStyle.Fixes','Microsoft.CodeAnalysis.'+language+'.CodeStyle','Microsoft.CodeAnalysis.'+language+'.CodeStyle.Fixes']
  base='analyzers/dotnet/'+short+'/'
  wanted_dlls=[base+a+'.dll' for a in names]+[base+c+'/'+a+'.resources.dll' for c in cultures for a in names]
  assert sorted(p for p in z.namelist() if p.endswith('.dll'))==sorted(wanted_dlls)
  dlls[language]={p.removeprefix(base):z.read(p) for p in wanted_dlls}
  for filename in ['source-items.json','localization.json','removed-binaries.json']:assert z.read('source-build/'+filename)==payload[doc+filename]
  for filename in ['ExpectedAssemblies.json','ExpectedBuildAssets.json','UpstreamFixtures.json','published-metadata-provenance.json']:assert z.read('source-build/'+filename)==(r/filename).read_bytes()
  assert z.read('README.md')==(r/'README.md').read_bytes()
  for src,target in [('License.txt','License.txt'),('THIRD-PARTY-NOTICES.txt','THIRD-PARTY-NOTICES.txt'),('src/Setup/PowerShell/install.ps1','tools/install.ps1'),('src/Setup/PowerShell/uninstall.ps1','tools/uninstall.ps1')]:assert z.read(target)==original[src]
  for path,text in build_assets[language].items():check_text(z.read(path),text)
  assert {p for p in z.namelist() if p.startswith('build/')}==set(build_assets[language])
  source_map=json.loads(z.read('source-build/source-map.json'));assert len(set(source_map.values()))==len(source_map)
  for path,target in source_map.items():assert z.read(target)==original[path],path
  for name,entry in sources.items():
   project=entry['original_project'];parent=posixpath.dirname(project);extension='.vb' if project.endswith('.vbproj') else '.cs'
   wanted_files={p for p in original if p.startswith(parent+'/') and p.endswith(extension)};visited=set();wanted_resources=set()
   def collect(path):
    if path in visited:return
    visited.add(path);tree=E.fromstring(original[path]);parent=posixpath.dirname(path)
    for e in tree.iter():
     kind=tag(e)
     if kind=='Import' and e.get('Project'):collect(resolve(parent,e.get('Project')))
     if kind=='Compile':
      assert not e.get('Remove') and e.get('Condition') in {None,"'$(DotNetBuildFromSource)' == 'true'"}
      wanted_files.add(resolve(parent,e.get('Include')))
     if kind=='EmbeddedResource':wanted_resources.add(resolve(parent,e.get('Include') or e.get('Update')))
   collect(project)
   assert set(entry['source_paths'])==wanted_files,(name,'source coverage')
   assert set(entry['imports'])==visited,(name,'import coverage')
   assert set(entry['resources'])==wanted_resources,(name,'resource coverage')
   for path in wanted_files:assert hashlib.sha256(original[path]).hexdigest()==entry['source_sha256'][path] and path in source_map,path
   for path in visited|wanted_resources:assert path in source_map,path
   for res in wanted_resources:
    parent=posixpath.dirname(res);stem=Path(res).stem
    for path in original:
     if path.startswith(parent+'/xlf/'+stem+'.') and path.endswith('.xlf'):assert path in source_map,path
  for name in ['prepare.py','build-libraries.sh','build.sh','pack.py','ExtractFixtures.cs']:assert z.read('source-build/recipe/'+name)==(r/name).read_bytes(),name
 records[language]=dict(artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),nuget_sha256=hashlib.sha256(payload[package_path]).hexdigest(),dll_assets={p:hashlib.sha256(b).hexdigest() for p,b in dlls[language].items()})
for name in dlls['CSharp']:
 if 'CSharp' not in name:assert dlls['CSharp'][name]==dlls['VisualBasic'][name],name
tested=json.loads(Path('tests/powershell-deps/codestyle-tested-dlls-20261027.json').read_text())
assert len(tested)==84
for language,assets in dlls.items():
 for path,content in assets.items():assert tested[path]==hashlib.sha256(content).hexdigest(),(language,path,'runtime-tested bytes')
artifact=r/'mingw-w64-clang-x86_64-dotnet-sdk-codestyle-4.9.2-1-any.pkg.tar.zst';sdk=read_package(artifact)
info=sdk['.PKGINFO'].decode();assert sorted(s[9:] for s in info.splitlines() if s.startswith('depend = '))==sorted('mingw-w64-clang-x86_64-'+s for s in ['dotnet-sdk=8.0.131','dotnet-codestyle-csharp=4.9.2','dotnet-codestyle-visualbasic=4.9.2'])
expected_sdk=set()
for language,short in [('CSharp','cs'),('VisualBasic','vb')]:
 base='clang64/lib/dotnet/sdk/8.0.131/Sdks/Microsoft.NET.Sdk/codestyle/'+short+'/'
 for path,content in dlls[language].items():assert sdk[base+path]==content;expected_sdk.add(base+path)
 for path,text in build_assets[language].items():
  if path.endswith('.props'):assert base+path not in sdk
  else:check_text(sdk[base+path],text);expected_sdk.add(base+path)
for path in sdk:
 if '/lib/dotnet/' in path:assert path in expected_sdk,path
for name in ['License.txt','THIRD-PARTY-NOTICES.txt']:assert sdk['clang64/share/licenses/dotnet-sdk-codestyle/'+name]==original[name]
records['SDK']=dict(artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),sdk_files=len(expected_sdk))
record=dict(recipe=str(r),source_commit=commit,source_sha256=source_sha,original_source_counts=source_counts,production_original_inputs=sum(list(source_counts.values())[:-1]),configuration_generator_inputs=5,analyzers=135,fixers=172,api_declarations=100,diagnostic_id_constants=138,resource_sets=210,resource_strings=5684,selected_upstream_cases=146,all_analyzer_language_runs=137,codefix_action_providers=11,runtime_tested_unique_dlls=84,nuget_dll_assets_byte_matched=112,artifacts=records,scope='full original CodeStyle analyzer/fixer source and SDK integration; selected behavior checks, not full IDE/xUnit suite')
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(record,indent=2)+'\n')
print('PASS CodeStyle source/checksums/licenses, complete1561+5 original inputs,112 NuGet DLL assets match84 runtime-tested DLLs, exact shared copies, SDK payload and independent API/analyzer/fixer/resource/config baselines')
