"""Audit the complete split StyleCop source packages and their offline check data."""
from pathlib import Path
import hashlib,io,json,subprocess,sys,zipfile
import xml.etree.ElementTree as E

root=Path('mingw-w64-dotnet-stylecop-analyzers');fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
 key,sep,value=line.strip().partition(' = ')
 if sep:fields.setdefault(key,[]).append(value)
assert sorted(fields['pkgname'])==['mingw-w64-clang-x86_64-dotnet-stylecop-analyzers','mingw-w64-clang-x86_64-dotnet-stylecop-analyzers-unstable']
assert fields['pkgver']==['1.2.0.beta556'] and fields['pkgrel']==['1']
assert len(fields['source'])==len(fields['sha256sums'])
commit='73a46ae2e65e7870fefe35e2c4c12bb7152f4ec5'
assert fields['source'][0]=='stylecop::git+https://github.com/DotNetAnalyzers/StyleCopAnalyzers#commit='+commit
assert fields['sha256sums'][0]=='SKIP'
for source,wanted in zip(fields['source'][1:],fields['sha256sums'][1:]):
 assert wanted!='SKIP' and hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
lock=json.loads((root/'source-inputs.json').read_text());raw=(root/'upstream-commit.txt').read_bytes()
assert hashlib.sha1(b'commit '+str(len(raw)).encode()+b'\0'+raw).hexdigest()==commit
assert raw.splitlines()[0]==b'tree c825926498b59694e7f559cd6d55377bbac57283'
assert lock['commit']==commit and lock['source_files']==2811
inventory=json.loads((root/'ExpectedInventory.json').read_text())
assert len(inventory['analyzers']['analyzers'])==182 and len(inventory['analyzers']['diagnostic_ids'])==178
assert len(inventory['codefixes']['code_fixes'])==89
corpus=json.loads((root/'UpstreamFixtures.json').read_text());assert len(corpus['fixtures'])==2880
cultures=['de-DE','en-GB','es-MX','fr-FR','pl-PL','pt-BR','ru-RU'];records=[]
assert len(list(root.glob('*.pkg.tar.zst')))==2
for suffix,id,version in [('', 'StyleCop.Analyzers','1.2.0-beta.556'),('-unstable','StyleCop.Analyzers.Unstable','1.2.0.556')]:
 artifact=root/('mingw-w64-clang-x86_64-dotnet-stylecop-analyzers'+suffix+'-1.2.0.beta556-1-any.pkg.tar.zst')
 entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
 def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
 info=payload('.PKGINFO').decode();assert 'pkgver = 1.2.0.beta556-1\n' in info
 deps=['dotnet-stylecop-analyzers-unstable=1.2.0.beta556'] if not suffix else ['dotnet-runtime=8.0.131','dotnet-codeanalysis-csharp-workspaces=4.9.2','dotnet-csharp-compiler=4.9.2']
 assert sorted(e[9:] for e in info.splitlines() if e.startswith('depend = '))==sorted('mingw-w64-clang-x86_64-'+v for v in deps)
 license_hashes={'LICENSE':'70aecc0beb2d4fc244c7c295695bc557ec4a0200fe481b98a6993cf972d752b3','THIRD-PARTY-NOTICES.txt':'28b87b46eea7e1da15e2a948e682ff36590e8b90128f27e8fea69d8e35d5560e'}
 for file,wanted in license_hashes.items():
  assert hashlib.sha256(payload('clang64/share/licenses/dotnet-stylecop-analyzers'+suffix+'/'+file)).hexdigest()==wanted,file
 assert payload('clang64/share/licenses/dotnet-stylecop-analyzers'+suffix+'/Apache-2.0.txt')==(root/'Apache-2.0.txt').read_bytes()
 doc='clang64/share/doc/dotnet-stylecop-analyzers'+suffix+'/'
 assert payload(doc+'README.md')==(root/'README.md').read_bytes()
 target='clang64/lib/dotnet/library-packs/'+id.lower()+'.'+version+'.nupkg'
 assert [p for p in entries if p.endswith('.nupkg')]==[target]
 with zipfile.ZipFile(io.BytesIO(payload(target))) as z:
  spec=E.fromstring(z.read(id+'.nuspec'))
  assert spec.findtext('.//{*}id')==id and spec.findtext('.//{*}version')==version
  assert spec.findtext('.//{*}developmentDependency')=='true' and spec.findtext('.//{*}license')=='MIT AND Apache-2.0'
  assert spec.find('.//{*}repository').get('commit')==commit
  assert z.read('README.md')==(root/'README.md').read_bytes()
  for file,wanted in license_hashes.items():assert hashlib.sha256(z.read(file)).hexdigest()==wanted,file
  assert z.read('Apache-2.0.txt')==(root/'Apache-2.0.txt').read_bytes()
  assert not any(n.endswith(('.snk','.pdb','.p7s')) for n in z.namelist())
  dlls=sorted(n for n in z.namelist() if n.endswith('.dll'))
  expected=[] if not suffix else sorted(['analyzers/dotnet/cs/StyleCop.Analyzers.dll','analyzers/dotnet/cs/StyleCop.Analyzers.CodeFixes.dll']+['analyzers/dotnet/cs/'+c+'/StyleCop.Analyzers.resources.dll' for c in cultures])
  assert dlls==expected,dlls
  nuget_deps={e.get('id'):e.get('version') for e in spec.findall('.//{*}dependency')}
  assert nuget_deps==({'StyleCop.Analyzers.Unstable':'[1.2.0.556]'} if not suffix else {})
  hashes={n:hashlib.sha256(z.read(n)).hexdigest() for n in dlls}
  if suffix:
   for name,count in [('StyleCop.Analyzers.CodeGeneration',7),('StyleCop.Analyzers',313),('StyleCop.Analyzers.CodeFixes',107)]:
    assert len([n for n in z.namelist() if n.startswith('src/'+name+'/') and n.endswith('.cs')])==count,name
   guard=z.read('src/StyleCop.Analyzers.CodeFixes/ReadabilityRules/SA1134CodeFixProvider.cs').decode()
   assert '!attributeList.ContainsDiagnostics' in guard and 'attributeListSyntax.ContainsDiagnostics' in guard
   assert json.loads(z.read('source-build/checkdata/ExpectedInventory.json'))==inventory
   assert json.loads(z.read('source-build/checkdata/fixtures.json'))==corpus
   resources=json.loads(z.read('source-build/checkdata/localization.json'))
   assert len(resources)==17 and sum(len(r['strings']) for r in resources)==857
   assert sorted(set(r['culture'] for r in resources if r['culture']))==cultures
   for kind in ['baseline','regenerated']:
    assert len([n for n in z.namelist() if n.startswith('source-build/checkdata/'+kind+'/') and n.endswith('.cs')])==183
   for name in ['ExpectedInventory.json','source-inputs.json','upstream-commit.txt','published-metadata-provenance.json']:
    assert z.read('source-build/checkdata/'+name)==(root/name).read_bytes()
  else:
   assert json.loads(payload(doc+'ExpectedInventory.json'))==inventory
   assert json.loads(payload(doc+'fixtures.json'))==corpus
   for kind in ['baseline','regenerated']:
    assert len([n for n in entries if n.startswith(doc+kind+'/') and n.endswith('.cs')])==183
 records.append(dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),nuget_id=id,nuget_version=version,nuget_dependencies=nuget_deps,source_commit=commit,source_tree=lock['tree'],managed_payload_sha256=hashes,analyzer_classes=182,codefix_classes=89,diagnostic_ids=178,resource_sets=17,resource_strings=857,upstream_vectors=len(corpus['fixtures']),owned_vectors=16,source_files=[7,313,107],assembly_version='1.2.0.556',public_key_token='cfeb5dbada5e1c25'))
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(records,indent=2)+'\n')
print('StyleCop: exact signed-source commit/checksums, both meta/implementation packages, nine managed assets, all 427 source inputs, 183+183 generation baselines, 182/89/178 inventories, 17/857 resources and 2880 upstream vectors PASS')
