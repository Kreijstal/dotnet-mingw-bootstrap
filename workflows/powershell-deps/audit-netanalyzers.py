"""Independently audit exact SDK-selected NetAnalyzers packages and source coverage."""
from pathlib import Path
import hashlib,io,json,posixpath,subprocess,sys,tarfile,zipfile
import xml.etree.ElementTree as E
r=Path('mingw-w64-dotnet-netanalyzers');fields={}
for line in (r/'.SRCINFO').read_text().splitlines():
 key,sep,value=line.strip().partition(' = ')
 if sep:fields.setdefault(key,[]).append(value)
version='8.0.0.preview26274.2';nuget='8.0.0-preview.26274.2';commit='0963af6bfc2a3d5083faa2b7c1c91a4eee29a6f4';source_sha='1e4434f9401670f8b0e53116edecbb3a5db60c056dab5ac2c28a88966d9861e0'
assert fields['pkgver']==[version] and fields['pkgrel']==['1']
assert len(fields['source'])==len(fields['sha256sums'])==23 and fields['sha256sums'][0]==source_sha
for name,sha in zip(fields['source'],fields['sha256sums']):
 assert sha!='SKIP'
 if '::' not in name:assert hashlib.sha256((r/name).read_bytes()).hexdigest()==sha,name
archive=Path('/tmp/powershell-netanalyzers')/('roslyn-analyzers-'+commit+'.tar.gz')
if not archive.exists():archive=r/('roslyn-analyzers-'+commit+'.tar.gz')
assert hashlib.sha256(archive.read_bytes()).hexdigest()==source_sha
with tarfile.open(archive) as t:
 prefix='roslyn-analyzers-'+commit+'/'
 original={m.name.removeprefix(prefix):t.extractfile(m).read() for m in t.getmembers() if m.isfile() and m.name.startswith(prefix)}
def package(path):
 data=subprocess.check_output(['zstd','-dc',str(path)])
 with tarfile.open(fileobj=io.BytesIO(data)) as t:return {m.name:t.extractfile(m).read() for m in t.getmembers() if m.isfile()}
def resolve(parent,path):
 path=path.replace('$(MSBuildThisFileDirectory)',parent+'/').replace('\\','/')
 assert '$(' not in path and '*' not in path
 return posixpath.normpath(path if path.startswith('src/') else parent+'/'+path)
artifact=r/('mingw-w64-clang-x86_64-dotnet-netanalyzers-'+version+'-1-any.pkg.tar.zst');payload=package(artifact)
info=payload['.PKGINFO'].decode();assert 'pkgver = '+version+'-1\n' in info
wanted=['dotnet-runtime=8.0.131','dotnet-codeanalysis-csharp-workspaces=4.9.2','dotnet-codeanalysis-visualbasic-workspaces=4.9.2','dotnet-csharp-compiler=4.9.2','dotnet-visualbasic-compiler=4.9.2']
assert sorted(x[9:] for x in info.splitlines() if x.startswith('depend = '))==sorted('mingw-w64-clang-x86_64-'+x for x in wanted)
base='clang64/share/doc/dotnet-netanalyzers/'
for file in ['README.md','ExpectedAssemblies.json','ExpectedLegacyEmpty.json','ExpectedGeneratedAssets.json','UpstreamFixtures.json','published-metadata-provenance.json']:assert payload[base+file]==(r/file).read_bytes(),file
sources=json.loads(payload[base+'source-items.json']);resources=json.loads(payload[base+'localization.json']);fixtures=json.loads(payload[base+'fixtures.json'])
assert fixtures==json.loads((r/'UpstreamFixtures.json').read_text())
assert len(fixtures['fixtures'])==938 and sum(e['label'].startswith('owned:') for e in fixtures['fixtures'])==16
assert [v['source_files'] for v in sources.values()]==[722,122,107,15]
assert len(resources)==42 and sum(len(e['strings']) for e in resources)==15260
expected=json.loads((r/'ExpectedAssemblies.json').read_text())
assert sum(len(v['api']['api']) for v in expected.values())==3471
assert sum(len(v['inventory']['analyzers']) for v in expected.values())==293
assert sum(len(v['inventory']['code_fixes']) for v in expected.values())==189
assert sum(len(v['inventory']['diagnostic_ids']) for v in expected.values())==232
legacy=json.loads((r/'ExpectedLegacyEmpty.json').read_text());assert len(legacy)==53
for entry in legacy:
 assert hashlib.sha256(original[entry['path']]).hexdigest()==entry['sha256']
 assert ('ImmutableArray<DiagnosticDescriptor>.Empty' if entry['kind']=='analyzer' else 'ImmutableArray<string>.Empty') in original[entry['path']].decode('utf-8-sig')
package_path='clang64/lib/dotnet/library-packs/microsoft.codeanalysis.netanalyzers.'+nuget+'.nupkg'
assert [p for p in payload if p.endswith('.nupkg')]==[package_path]
cultures=['cs','de','es','fr','it','ja','ko','pl','pt-BR','ru','tr','zh-Hans','zh-Hant'];dlls={};build_assets=json.loads((r/'ExpectedGeneratedAssets.json').read_text())
assert len(build_assets)==611
ports={
 'TypesThatOwnDisposableFieldsShouldBeDisposable.cs':[('disposableFieldNamesBuilder.AsEnumerable_NonConcurrent().Order()','Analyzer.Utilities.Extensions.IEnumerableExtensions.Order(disposableFieldNamesBuilder.AsEnumerable_NonConcurrent())')],
 'GlobalFlowStateAnalysisValueSet.cs':[('parentsBuilder.Order()','Analyzer.Utilities.Extensions.IEnumerableExtensions.Order(parentsBuilder)'),('AnalysisValues.Select(f => f.ToString()).Order()','Analyzer.Utilities.Extensions.IEnumerableExtensions.Order(AnalysisValues.Select(f => f.ToString()))')],
 'DoNotPassLiteralsAsLocalizedParameters.cs':[('literalValues.Order()','Analyzer.Utilities.Extensions.IEnumerableExtensions.Order(literalValues)')],
 'CSharpUseStringMethodCharOverloadWithSingleCharacters.Fixer.cs':[('SyntaxFactory.SeparatedList(arguments)','SyntaxFactory.SeparatedList(arguments.Cast<ArgumentSyntax>())')]}
with zipfile.ZipFile(io.BytesIO(payload[package_path])) as z:
 spec=E.fromstring(z.read('Microsoft.CodeAnalysis.NetAnalyzers.nuspec'))
 assert spec.findtext('.//{*}version')==nuget and spec.findtext('.//{*}license')=='MIT'
 assert spec.find('.//{*}repository').get('commit')==commit and not spec.findall('.//{*}dependency')
 assert spec.findtext('.//{*}developmentDependency')=='true'
 assert not any(p.endswith(('.snk','.pdb','.p7s','.nupkg')) for p in z.namelist())
 names=list(expected);core='Microsoft.CodeAnalysis.NetAnalyzers.dll';wanted_dlls=[]
 for language,leaf in [('cs','Microsoft.CodeAnalysis.CSharp.NetAnalyzers.dll'),('vb','Microsoft.CodeAnalysis.VisualBasic.NetAnalyzers.dll')]:
  prefix='analyzers/dotnet/'+language+'/'
  wanted_dlls.extend([prefix+core,prefix+leaf]+[prefix+c+'/Microsoft.CodeAnalysis.NetAnalyzers.resources.dll' for c in cultures])
 assert sorted(p for p in z.namelist() if p.endswith('.dll'))==sorted(wanted_dlls)
 for path in wanted_dlls:
  relative=path.split('/',3)[3];content=z.read(path)
  if relative in dlls:assert dlls[relative]==content,relative
  dlls[relative]=content
 for path,text in build_assets.items():assert z.read(path).decode('utf-8-sig').replace('\r\n','\n')==text,path
 assert {p for p in z.namelist() if p.startswith(('buildTransitive/','rulesets/','editorconfig/','documentation/'))}==set(build_assets)
 for src,target in [('License.txt','License.txt'),('assets/ThirdPartyNotices.txt','ThirdPartyNotices.txt'),('assets/install.ps1','tools/install.ps1'),('assets/uninstall.ps1','tools/uninstall.ps1'),('docs/Analyzer Configuration.md','documentation/Analyzer Configuration.md'),('src/NetAnalyzers/readme.md','documentation/readme.md')]:assert z.read(target)==original[src],src
 for file in ['source-items.json','removed-binaries.json','localization.json','fixtures.json']:assert z.read('source-build/'+file)==payload[base+file]
 for file in ['ExpectedAssemblies.json','ExpectedLegacyEmpty.json','ExpectedGeneratedAssets.json','UpstreamFixtures.json','published-metadata-provenance.json']:assert z.read('source-build/'+file)==(r/file).read_bytes()
 source_map=json.loads(z.read('source-build/source-map.json'))
 for path,target in source_map.items():assert z.read(target)==original[path],path
 overlay_count=0
 for name,entry in sources.items():
  if name=='config-generator':folder='src/Tools/GenerateDocumentationAndConfigFiles';project=folder+'/GenerateDocumentationAndConfigFiles.csproj';extension='.cs'
  else:
   language={'Microsoft.CodeAnalysis.NetAnalyzers':'Core','Microsoft.CodeAnalysis.CSharp.NetAnalyzers':'CSharp','Microsoft.CodeAnalysis.VisualBasic.NetAnalyzers':'VisualBasic'}[name];folder='src/NetAnalyzers/'+language;extension='.vb' if language=='VisualBasic' else '.cs';project=folder+'/'+name+extension+'proj'
  files={p for p in original if p.startswith(folder+'/') and p.endswith(extension)};visited=set()
  def collect(path):
   if path in visited:return
   visited.add(path);tree=E.fromstring(original[path]);parent=posixpath.dirname(path)
   for e in tree.iter():
    kind=e.tag.rsplit('}',1)[-1]
    if kind=='Import' and e.get('Project'):collect(resolve(parent,e.get('Project')))
    if kind=='Compile' and e.get('Include'):assert not e.get('Remove');files.add(resolve(parent,e.get('Include')))
  collect(project)
  assert files==set(entry['source_paths']),(name,'source coverage')
  assert visited==set(entry['imports']),(name,'original project/shared imports')
  for path in files:assert hashlib.sha256(original[path]).hexdigest()==entry['source_sha256'][path] and path in source_map,path
  for path,change in entry.get('source_substitutions',{}).items():
   replacements=ports[Path(path).name];assert [list(v) for v in replacements]==change['changes']
   text=original[path].decode('utf-8-sig')
   for old,new in replacements:assert text.count(old)==1;text=text.replace(old,new)
   data=text.encode();assert z.read('s/ported/'+Path(path).name)==data and hashlib.sha256(data).hexdigest()==change['sha256'];overlay_count+=1
  if name!='config-generator':
   for file in ['AnalyzerReleases.Shipped.md','AnalyzerReleases.Unshipped.md']:assert z.read('source-build/releases/'+language+'/'+file)==original[folder+'/'+file]
 assert overlay_count==4
 for entry in resources:
  if entry['culture']!='':continue
  path=next(p for p in original if p.endswith('/'+entry['resource'].split('.')[-1]+'.resx') and p.startswith('src/NetAnalyzers/Core/'))
  for xlf in original:
   if xlf.startswith(posixpath.dirname(path)+'/xlf/'+Path(path).stem+'.') and xlf.endswith('.xlf'):assert xlf in source_map,xlf
 for file in ['prepare.py','build-libraries.sh','generate-config.sh','ExtractFixtures.cs','pack.py','verify-generated.py']:assert z.read('source-build/recipe/'+file)==(r/file).read_bytes()
for file,original_path in [('License.txt','License.txt'),('ThirdPartyNotices.txt','assets/ThirdPartyNotices.txt')]:assert payload['clang64/share/licenses/dotnet-netanalyzers/'+file]==original[original_path]
tested=json.loads(Path('tests/powershell-deps/netanalyzers-tested-dlls-20261027.json').read_text())
assert len(tested)==16
for name,content in dlls.items():assert hashlib.sha256(content).hexdigest()==tested[name],name
sdk_artifact=r/('mingw-w64-clang-x86_64-dotnet-sdk-netanalyzers-'+version+'-1-any.pkg.tar.zst');sdk=package(sdk_artifact);sdk_info=sdk['.PKGINFO'].decode()
assert sorted(x[9:] for x in sdk_info.splitlines() if x.startswith('depend = '))==sorted(['mingw-w64-clang-x86_64-dotnet-sdk>=8.0.131-2','mingw-w64-clang-x86_64-dotnet-netanalyzers='+version])
prefix='clang64/lib/dotnet/sdk/8.0.131/Sdks/Microsoft.NET.Sdk/analyzers/'
expected_sdk=set()
for path,data in dlls.items():assert sdk[prefix+path]==data;expected_sdk.add(prefix+path)
for path,text in build_assets.items():
 if path.startswith('buildTransitive/config/'):
  target=prefix+'build/config/'+path.removeprefix('buildTransitive/config/');assert sdk[target].decode('utf-8-sig').replace('\r\n','\n')==text;expected_sdk.add(target)
for source,target in [('buildTransitive/Microsoft.CodeAnalysis.NetAnalyzers.props','build/SourceBuild.NetAnalyzers.Default.props'),('buildTransitive/Microsoft.CodeAnalysis.NetAnalyzers.targets','build/SourceBuild.NetAnalyzers.targets')]:
 assert sdk[prefix+target].decode('utf-8-sig').replace('\r\n','\n')==build_assets[source];expected_sdk.add(prefix+target)
assert sdk[prefix+'build/SourceBuild.NetAnalyzers.props']==(r/'SourceBuild.NetAnalyzers.props').read_bytes();expected_sdk.add(prefix+'build/SourceBuild.NetAnalyzers.props')
assert {p for p in sdk if p.startswith('clang64/lib/dotnet/')}==expected_sdk
assert not any('DisableNETAnalyzersForNuGetPackage.props' in p for p in sdk)
for file,original_path in [('License.txt','License.txt'),('ThirdPartyNotices.txt','assets/ThirdPartyNotices.txt')]:assert sdk['clang64/share/licenses/dotnet-sdk-netanalyzers/'+file]==original[original_path]
bootstrap=Path('mingw-w64-dotnet-bootstrap/mingw-w64-clang-x86_64-dotnet-sdk-8.0.131-2-any.pkg.tar.zst');bootstrap_payload=package(bootstrap)
for extension in ['props','targets']:
 path=prefix+'build/Microsoft.CodeAnalysis.NetAnalyzers.'+extension
 tree=E.fromstring(bootstrap_payload[path]);item=tree.find('Import');assert item is not None and 'SourceBuild.NetAnalyzers.'+extension in item.get('Project') and item.get('Condition').startswith('Exists(')
 assert path not in sdk
record=dict(recipe=str(r),source_commit=commit,source_sha256=source_sha,nuget_version=nuget,original_source_counts={k:v['source_files'] for k,v in sources.items()},source_overlays=4,public_api_declarations=3471,analyzers=293,fixers=189,id_constants=232,resource_sets=42,resource_strings=15260,generated_files=609,all_baselined_text_files=611,nuget_dll_assets=30,runtime_tested_unique_dlls=16,unique_dlls={k:hashlib.sha256(v).hexdigest() for k,v in dlls.items()},sdk_files=len(expected_sdk),upstream_cases=922,owned_cases=16,artifacts={artifact.name:hashlib.sha256(artifact.read_bytes()).hexdigest(),sdk_artifact.name:hashlib.sha256(sdk_artifact.read_bytes()).hexdigest()},sdk_bootstrap_forwarder_artifact=dict(name=bootstrap.name,sha256=hashlib.sha256(bootstrap.read_bytes()).hexdigest()),scope='complete original .NET analyzer/fixer source, full generated assets, original compatibility no-op declarations and explicit selected behavior/reference profiles; not full IDE/xUnit suite')
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(record,indent=2)+'\n')
print('PASS NetAnalyzers exact source/checksums/licenses,951+15 original inputs/four overlays,3471 API/293 analyzer/189 fixer inventory,30 DLL assets,611 text assets, SDK provider and separate bootstrap import ownership')
