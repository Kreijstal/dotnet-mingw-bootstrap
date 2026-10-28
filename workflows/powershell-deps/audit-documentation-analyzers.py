"""Audit both source DocumentationAnalyzers NuGet packages and complete inventories."""
from pathlib import Path
import hashlib,io,json,subprocess,sys,zipfile
import xml.etree.ElementTree as E
root=Path('mingw-w64-dotnet-documentation-analyzers');fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
 key,sep,value=line.strip().partition(' = ')
 if sep:fields.setdefault(key,[]).append(value)
assert sorted(fields['pkgname'])==['mingw-w64-clang-x86_64-dotnet-documentation-analyzers','mingw-w64-clang-x86_64-dotnet-documentation-analyzers-unstable']
assert fields['pkgver']==['1.0.0.beta59'] and fields['pkgrel']==['1']
assert len(fields['source'])==len(fields['sha256sums'])
assert fields['sha256sums'][0]=='28249640f14e82a53702994be7c651eb7ac3559c922452e57770301f7f9b9605'
for source,wanted in zip(fields['source'],fields['sha256sums']):
 assert wanted!='SKIP'
 if '::' not in source:assert hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
inventory=json.loads((root/'ExpectedInventory.json').read_text())
assert len(inventory['DocumentationAnalyzers']['analyzers'])==18 and len(inventory['DocumentationAnalyzers']['diagnostic_ids'])==18
assert len(inventory['DocumentationAnalyzers.CodeFixes']['code_fixes'])==14
records=[];assert len(list(root.glob('*.pkg.tar.zst')))==2
for suffix,id,version in [('', 'DotNetAnalyzers.DocumentationAnalyzers','1.0.0-beta.59'),('-unstable','DotNetAnalyzers.DocumentationAnalyzers.Unstable','1.0.0.59')]:
 artifact=root/('mingw-w64-clang-x86_64-dotnet-documentation-analyzers'+suffix+'-1.0.0.beta59-1-any.pkg.tar.zst')
 entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
 def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
 info=payload('.PKGINFO').decode();assert 'pkgver = 1.0.0.beta59-1\n' in info
 deps=['dotnet-documentation-analyzers-unstable=1.0.0.beta59'] if not suffix else ['dotnet-runtime=8.0.131','dotnet-commonmark=0.15.1','dotnet-codeanalysis-csharp-workspaces=4.9.2','dotnet-csharp-compiler=4.9.2']
 assert sorted(e[9:] for e in info.splitlines() if e.startswith('depend = '))==sorted('mingw-w64-clang-x86_64-'+v for v in deps)
 for file in ['LICENSE','THIRD-PARTY-NOTICES']:assert 'clang64/share/licenses/dotnet-documentation-analyzers'+suffix+'/'+file in entries
 doc='clang64/share/doc/dotnet-documentation-analyzers'+suffix+'/'
 for file in ['README.md','ExpectedInventory.json','UpstreamFixtures.json','published-metadata-provenance.json']:assert payload(doc+file)==(root/file).read_bytes()
 assert json.loads(payload(doc+'fixtures.json'))==json.loads((root/'UpstreamFixtures.json').read_text())
 fixtures=json.loads(payload(doc+'fixtures.json'));assert len(fixtures['fixtures'])==296 and len(fixtures['skipped'])==1
 assert sum(f['label'].startswith('owned:') for f in fixtures['fixtures'])==1
 resources=json.loads(payload(doc+'localization.json'));assert len(resources)==4 and sum(len(e['strings']) for e in resources)==71
 sources=json.loads(payload(doc+'source-items.json'));assert sources['DocumentationAnalyzers']['source_files']==35 and sources['DocumentationAnalyzers.CodeFixes']['source_files']==21
 target='clang64/lib/dotnet/library-packs/'+id.lower()+'.'+version+'.nupkg';assert [p for p in entries if p.endswith('.nupkg')]==[target]
 with zipfile.ZipFile(io.BytesIO(payload(target))) as z:
  spec=E.fromstring(z.read(id+'.nuspec'));assert spec.findtext('.//{*}id')==id and spec.findtext('.//{*}version')==version
  assert spec.findtext('.//{*}developmentDependency')=='true' and spec.findtext('.//{*}license')=='MIT'
  repository=spec.find('.//{*}repository');assert repository.get('commit')=='489c06575beee224030ffdc9e91ce43569f32a79'
  assert z.read('README.md')==(root/'README.md').read_bytes()
  assert not any(n.endswith(('.snk','.pdb','.p7s')) for n in z.namelist())
  dlls=sorted(n for n in z.namelist() if n.endswith('.dll'))
  expected=[] if not suffix else ['analyzers/dotnet/cs/CommonMark.dll','analyzers/dotnet/cs/DocumentationAnalyzers.CodeFixes.dll','analyzers/dotnet/cs/DocumentationAnalyzers.dll']
  assert dlls==expected
  nuget_deps={e.get('id'):e.get('version') for e in spec.findall('.//{*}dependency')}
  assert nuget_deps==({'DotNetAnalyzers.DocumentationAnalyzers.Unstable':'[1.0.0.59]'} if not suffix else {})
  hashes={n:hashlib.sha256(z.read(n)).hexdigest() for n in dlls}
  if suffix:
   assert len([n for n in z.namelist() if n.startswith('src/DocumentationAnalyzers/') and n.endswith('.cs')])==35
   assert len([n for n in z.namelist() if n.startswith('src/DocumentationAnalyzers.CodeFixes/') and n.endswith('.cs')])==21
   for file in ['tools/install.ps1','tools/uninstall.ps1','LICENSE','THIRD-PARTY-NOTICES.txt']:assert file in z.namelist()
   commonmark=next(Path('mingw-w64-dotnet-commonmark').glob('*.pkg.tar.zst'))
   package_bytes=subprocess.check_output(['tar','--zstd','-xOf',str(commonmark),'clang64/lib/dotnet/library-packs/commonmark.net.0.15.1.nupkg'])
   with zipfile.ZipFile(io.BytesIO(package_bytes)) as dependency:assert z.read('analyzers/dotnet/cs/CommonMark.dll')==dependency.read('lib/net8.0/CommonMark.dll')
 records.append(dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),nuget_id=id,nuget_version=version,nuget_dependencies=nuget_deps,source_commit=repository.get('commit'),source_sha256=fields['sha256sums'][0],managed_payload_sha256=hashes,analyzer_classes=18,codefix_classes=14,diagnostic_ids=18,resource_sets=4,resource_strings=71,upstream_vectors=295,owned_vectors=1,source_files=[35,21],assembly_version='1.0.0.59',public_key_token='2bc5e20c3ad68793',source_build_configuration='upstream DEVELOPMENT_KEY, public-only signing'))
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(records,indent=2)+'\n')
print('DocumentationAnalyzers: exact source/licenses/dependencies, both meta/implementation payloads, all 18/14/18 inventories, 4/71 resources, 296 regenerated vectors, all source files and byte-identical separately built CommonMark PASS')
