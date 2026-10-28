"""Audit the complete source-built Roslyn analyzer package and independent source inventory."""
from pathlib import Path
import hashlib,io,json,posixpath,subprocess,sys,tarfile,zipfile
import xml.etree.ElementTree as E

root=Path('mingw-w64-dotnet-codeanalysis-analyzers');fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
 key,sep,value=line.strip().partition(' = ')
 if sep:fields.setdefault(key,[]).append(value)
assert fields['pkgname']==['mingw-w64-clang-x86_64-dotnet-codeanalysis-analyzers']
assert fields['pkgver']==['3.3.4'] and fields['pkgrel']==['1']
assert len(fields['source'])==len(fields['sha256sums'])==22
source_sha='49a992347546aef48cb31673b294bd1b10c30e68bbb53be61d411bb0d7219e1e';commit='22ea6422f85b05ca0793cc3b76375487be407f5d'
assert fields['sha256sums'][0]==source_sha
for name,sha in zip(fields['source'],fields['sha256sums']):
 assert sha!='SKIP'
 if '::' not in name:assert hashlib.sha256((root/name).read_bytes()).hexdigest()==sha,name
artifact=root/'mingw-w64-clang-x86_64-dotnet-codeanalysis-analyzers-3.3.4-1-any.pkg.tar.zst'
assert list(root.glob('*.pkg.tar.zst'))==[artifact]
entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
info=payload('.PKGINFO').decode();assert 'pkgver = 3.3.4-1\n' in info
expected_deps=['dotnet-runtime=8.0.131','dotnet-codeanalysis-csharp-workspaces=4.9.2','dotnet-codeanalysis-visualbasic-workspaces=4.9.2','dotnet-csharp-compiler=4.9.2','dotnet-visualbasic-compiler=4.9.2']
assert sorted(line[9:] for line in info.splitlines() if line.startswith('depend = '))==sorted('mingw-w64-clang-x86_64-'+d for d in expected_deps)
prefix='clang64/share/doc/dotnet-codeanalysis-analyzers/'
static=['README.md','ExpectedApi.json','ExpectedInventory.json','ExpectedMetadata.json','ExpectedGeneratedAssets.json','UpstreamFixtures.json','published-metadata-provenance.json']
for filename in static:assert payload(prefix+filename)==(root/filename).read_bytes(),filename
sources=json.loads(payload(prefix+'source-items.json'));names=['Microsoft.CodeAnalysis.Analyzers','Microsoft.CodeAnalysis.CSharp.Analyzers','Microsoft.CodeAnalysis.VisualBasic.Analyzers']
assert [sources[n]['source_files'] for n in names]==[152,11,11] and sources['config-generator']['source_files']==15
assert sum(len(e.get('source_substitutions',{})) for e in sources.values())==2
resources=json.loads(payload(prefix+'localization.json'));assert len(resources)==14 and sum(len(r['strings']) for r in resources)==2030
fixtures=json.loads(payload(prefix+'fixtures.json'));assert fixtures==json.loads((root/'UpstreamFixtures.json').read_text()) and len(fixtures['fixtures'])==70
expected_api=json.loads((root/'ExpectedApi.json').read_text());assert sum(len(v['api']) for v in expected_api.values())==325
inventory=json.loads((root/'ExpectedInventory.json').read_text());assert sum(len(v['analyzers']) for v in inventory.values())==23 and sum(len(v['code_fixes']) for v in inventory.values())==13 and sum(len(v['diagnostic_ids']) for v in inventory.values())==46
expected_assets=json.loads((root/'ExpectedGeneratedAssets.json').read_text())['normalized_sha256'];assert len(expected_assets)==554
package_path='clang64/lib/dotnet/library-packs/microsoft.codeanalysis.analyzers.3.3.4.nupkg'
assert [p for p in entries if p.endswith('.nupkg')]==[package_path]
archive=Path('/tmp/powershell-codeanalysis-analyzers/roslyn-analyzers-3.3.4.tar.gz')
if not archive.exists():archive=root/'roslyn-analyzers-3.3.4.tar.gz'
assert hashlib.sha256(archive.read_bytes()).hexdigest()==source_sha
with tarfile.open(archive) as original,zipfile.ZipFile(io.BytesIO(payload(package_path))) as z:
 base='roslyn-analyzers-'+commit+'/'
 def original_bytes(path):return original.extractfile(base+path).read()
 spec=E.fromstring(z.read('Microsoft.CodeAnalysis.Analyzers.nuspec'))
 assert spec.findtext('.//{*}id')=='Microsoft.CodeAnalysis.Analyzers' and spec.findtext('.//{*}version')=='3.3.4'
 assert spec.findtext('.//{*}developmentDependency')=='true' and spec.findtext('.//{*}license')=='MIT'
 assert spec.find('.//{*}repository').get('commit')==commit
 assert not spec.findall('.//{*}dependency')
 assert not any(p.endswith(('.pdb','.snk','.p7s','.nupkg')) for p in z.namelist())
 assert not any(Path(p).name in {'csc.dll','vbc.dll','Microsoft.CodeAnalysis.dll','Microsoft.CodeAnalysis.Workspaces.dll'} for p in z.namelist())
 cultures=['cs','de','es','fr','it','ja','ko','pl','pt-BR','ru','tr','zh-Hans','zh-Hant'];expected_dlls=[]
 for language,assembly in [('cs',names[1]),('vb',names[2])]:
  path='analyzers/dotnet/'+language+'/'
  expected_dlls.extend([path+names[0]+'.dll',path+assembly+'.dll']+[path+c+'/'+names[0]+'.resources.dll' for c in cultures])
 assert sorted(p for p in z.namelist() if p.endswith('.dll'))==sorted(expected_dlls)
 hashes={p:hashlib.sha256(z.read(p)).hexdigest() for p in expected_dlls}
 for path in expected_dlls:
  if '/vb/' in path and not path.endswith('/'+names[2]+'.dll'):assert z.read(path)==z.read(path.replace('/vb/','/cs/',1)),path
 for path,sha in expected_assets.items():assert hashlib.sha256(z.read(path).replace(b'\r\n',b'\n')).hexdigest()==sha,path
 generated_paths={p for p in z.namelist() if p.startswith(('buildTransitive/','editorconfig/','rulesets/','documentation/')) and p!='documentation/Analyzer Configuration.md'}
 assert generated_paths==set(expected_assets)
 for filename in static:assert z.read(filename if filename=='README.md' else 'source-build/'+filename)==(root/filename).read_bytes(),filename
 for filename in ['source-items.json','localization.json','fixtures.json','removed-binaries.json']:assert z.read('source-build/'+filename)==payload(prefix+filename),filename
 for src,target in [('License.txt','License.txt'),('assets/ThirdPartyNotices.txt','ThirdPartyNotices.txt'),('assets/install.ps1','tools/install.ps1'),('assets/uninstall.ps1','tools/uninstall.ps1'),('docs/Analyzer Configuration.md','documentation/Analyzer Configuration.md')]:assert z.read(target)==original_bytes(src),src
 for src,target in [('License.txt','License.txt'),('assets/ThirdPartyNotices.txt','ThirdPartyNotices.txt')]:assert payload('clang64/share/licenses/dotnet-codeanalysis-analyzers/'+target)==original_bytes(src)
 source_map=json.loads(z.read('source-build/source-map.json'));expected_sources=set()
 for path,target in source_map.items():assert z.read(target)==original_bytes(path),path
 def compact(path):return path.replace('src/Microsoft.CodeAnalysis.Analyzers/','src/analyzers/').replace('src/Tools/','src/tools/').replace('src/Utilities/','src/utilities/').replace('src/Microsoft.CodeAnalysis.BannedApiAnalyzers/','src/banned-api/')
 for language in ['Core','CSharp','VisualBasic']:
  for filename in ['AnalyzerReleases.Shipped.md','AnalyzerReleases.Unshipped.md']:
   path='src/Microsoft.CodeAnalysis.Analyzers/'+language+'/'+filename;assert z.read(compact(path))==original_bytes(path)
 xlf_prefix=base+'src/Microsoft.CodeAnalysis.Analyzers/Core/xlf/'
 for path in original.getnames():
  if path.startswith(xlf_prefix+'CodeAnalysisDiagnosticsResources.') and path.endswith('.xlf'):
   relative=path.removeprefix(base);assert z.read(compact(relative))==original_bytes(relative)
 ports={'PreferIsKindFix.cs':(': DocumentBasedFixAllProvider',': Analyzer.Utilities.DocumentBasedFixAllProvider'),'CSharpDiagnosticAnalyzerAPIUsageAnalyzer.cs':('or SyntaxKind.RecordDeclaration:','or SyntaxKind.RecordDeclaration')}
 for name,entry in sources.items():
  for path,sha in entry['source_sha256'].items():
   assert hashlib.sha256(original_bytes(path)).hexdigest()==sha,path
   assert z.read(source_map[path])==original_bytes(path),path
  for path,port in entry.get('source_substitutions',{}).items():
   text=original_bytes(path).decode('utf-8-sig');old,new=ports[Path(path).name];assert text.count(old)==1
   expected=text.replace(old,new).encode();target='src/ported/'+Path(path).stem+'.port.cs'
   assert z.read(target)==expected and hashlib.sha256(expected).hexdigest()==port['sha256'],path
  if name=='config-generator':main='src/Tools/GenerateDocumentationAndConfigFiles/';extension='.cs';project=main+'GenerateDocumentationAndConfigFiles.csproj'
  else:
   language={names[0]:'Core',names[1]:'CSharp',names[2]:'VisualBasic'}[name];main='src/Microsoft.CodeAnalysis.Analyzers/'+language+'/';extension='.vb' if language=='VisualBasic' else '.cs';project=main+name+extension+'proj'
  files={p.removeprefix(base) for p in original.getnames() if p.startswith(base+main) and p.endswith(extension)};visited=set()
  def collect(path):
   if path in visited:return
   visited.add(path);tree=E.fromstring(original_bytes(path));parent=posixpath.dirname(path)
   for e in tree.iter():
    tag=e.tag.rsplit('}',1)[-1]
    if tag=='Import' and e.get('Project'):collect(posixpath.normpath(parent+'/'+e.get('Project').replace('\\','/')))
    if tag=='Compile' and e.get('Include'):
     target=e.get('Include').replace('$(MSBuildThisFileDirectory)',parent+'/').replace('\\','/')
     if not target.startswith('src/'):target=parent+'/'+target
     files.add(posixpath.normpath(target))
  collect(project);assert files==set(entry['source_paths']),name;expected_sources.update(files)
 assert expected_sources.issubset(source_map)
record=dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),nuget_id='Microsoft.CodeAnalysis.Analyzers',nuget_version='3.3.4',source_commit=commit,source_sha256=source_sha,assembly_version='3.3.8.6701',public_key_token='31bf3856ad364e35',source_file_counts=[152,11,11],config_generator_source_files=15,independent_original_source_inventory_hashes_and_two_ports_match=True,api_declarations=325,analyzer_classes=23,exported_codefix_providers=13,diagnostic_id_constants=46,resource_sets=14,resource_strings=2030,generated_asset_count=554,managed_payload_sha256=hashes,nuget_implementation_dependencies=[],source_attachments=len(expected_sources),ancillary_scripts_docs_licenses_match_pinned_source=True)
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(record,indent=2)+'\n')
print('CodeAnalysis.Analyzers: pinned inputs/licenses/dependencies, complete 174+15 source inventory/two exact ports, all 30 DLL assets, 554 generated assets, scripts/docs/resources/API baselines and source attachments PASS')
