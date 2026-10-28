"""Audit source-built Roslyn Workspaces package payloads and dependency metadata."""
from pathlib import Path
import hashlib,io,json,subprocess,sys,zipfile
import xml.etree.ElementTree as E
root=Path('mingw-w64-dotnet-roslyn-workspaces');fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
 key,sep,value=line.strip().partition(' = ')
 if sep:fields.setdefault(key,[]).append(value)
assert fields['pkgver']==['4.9.2'] and fields['pkgrel']==['1']
assert len(fields['source'])==len(fields['sha256sums'])
assert fields['sha256sums'][0]=='e01ea67aaec6408de51a0a4c18a7e15b2a527ca7a853c9a0a7742cb5680aeb30'
for source,wanted in zip(fields['source'],fields['sha256sums']):
 assert wanted!='SKIP'
 if '::' not in source:assert hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
cultures=['cs','de','es','fr','it','ja','ko','pl','pt-BR','ru','tr','zh-Hans','zh-Hant']
expected_api=json.loads((root/'ExpectedApi.json').read_text());assert sum(len(v['api']) for v in expected_api.values())==1898
records=[];assert len(list(root.glob('*.pkg.tar.zst')))==2
for suffix,id,assembly,deps,pacman_deps in [
 ('workspaces-common','Microsoft.CodeAnalysis.Workspaces.Common','Microsoft.CodeAnalysis.Workspaces',{'Microsoft.CodeAnalysis.Common':'4.9.2','Humanizer.Core':'2.14.1','System.Composition':'8.0.0','System.IO.Pipelines':'8.0.0'},['dotnet-codeanalysis-common=4.9.2','dotnet-humanizer=2.14.1','dotnet-composition2=8.0.31','dotnet-io-pipelines=8.0.31']),
 ('csharp-workspaces','Microsoft.CodeAnalysis.CSharp.Workspaces','Microsoft.CodeAnalysis.CSharp.Workspaces',{'Microsoft.CodeAnalysis.Common':'4.9.2','Microsoft.CodeAnalysis.CSharp':'4.9.2','Microsoft.CodeAnalysis.Workspaces.Common':'4.9.2','Humanizer.Core':'2.14.1'},['dotnet-codeanalysis-common=4.9.2','dotnet-codeanalysis-csharp=4.9.2','dotnet-codeanalysis-workspaces-common=4.9.2','dotnet-humanizer=2.14.1'])]:
 artifact=root/('mingw-w64-clang-x86_64-dotnet-codeanalysis-'+suffix+'-4.9.2-1-any.pkg.tar.zst')
 entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
 def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
 info=payload('.PKGINFO').decode();assert 'pkgver = 4.9.2-1\n' in info
 assert sorted(e[9:] for e in info.splitlines() if e.startswith('depend = '))==sorted('mingw-w64-clang-x86_64-'+v for v in ['dotnet-runtime=8.0.131']+pacman_deps)
 for name in ['LICENSE','THIRD-PARTY-NOTICES']:assert 'clang64/share/licenses/dotnet-codeanalysis-'+suffix+'/'+name in entries
 doc='clang64/share/doc/dotnet-codeanalysis-'+suffix+'/'
 for name in ['README.md','ExpectedApi.json','published-metadata-provenance.json']:assert payload(doc+name)==(root/name).read_bytes()
 sources=json.loads(payload(doc+'source-items.json'))
 assert sources['Microsoft.CodeAnalysis.Workspaces']['source_files']==1467
 assert sources['Microsoft.CodeAnalysis.CSharp.Workspaces']['source_files']==272
 assert sources[assembly]['dependencies']==deps
 assert len(sources[assembly]['resources'])==(4 if suffix=='workspaces-common' else 3)
 assert all('$(' not in s for s in sources[assembly]['friend_attributes'])
 localized=json.loads(payload(doc+'localization.json'))
 assert len(localized)==98 and sum(len(s['strings']) for s in localized)==3780
 assert {e['culture'] for e in localized}==set(['']+cultures)
 removed=json.loads(payload(doc+'removed-binaries.json'));assert removed and all(len(e['sha256'])==64 for e in removed)
 target='clang64/lib/dotnet/library-packs/'+id.lower()+'.4.9.2.nupkg';assert [e for e in entries if e.endswith('.nupkg')]==[target]
 with zipfile.ZipFile(io.BytesIO(payload(target))) as z:
  spec=E.fromstring(z.read(id+'.nuspec'))
  assert spec.findtext('.//{*}id')==id and spec.findtext('.//{*}version')=='4.9.2'
  assert {e.get('id'):e.get('version') for e in spec.findall('.//{*}dependency')}=={k:'['+v+']' for k,v in deps.items()}
  repository=spec.find('.//{*}repository');assert repository.get('commit')=='9934fb9e3527e1c0c51314e57d4aab30f97e8f9e'
  assert spec.findtext('.//{*}license')=='MIT' and z.read('README.md')==(root/'README.md').read_bytes()
  dlls=[p for p in z.namelist() if p.endswith('.dll')]
  assert sorted(dlls)==sorted(['lib/net8.0/'+assembly+'.dll']+['lib/net8.0/'+c+'/'+assembly+'.resources.dll' for c in cultures]),dlls
  dll_hash=hashlib.sha256(z.read('lib/net8.0/'+assembly+'.dll')).hexdigest()
 records.append(dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),nuget_id=id,nuget_version='4.9.2',assembly_identity=expected_api[assembly]['identity'],published_api_declarations=len(expected_api[assembly]['api']),managed_sha256=dll_hash,source_commit=repository.get('commit'),source_sha256=fields['sha256sums'][0],dependencies=deps,satellite_cultures=cultures,source_files=sources[assembly]['source_files'],friend_declarations=sources[assembly]['friend_declarations'],source_build_configuration='DotNetBuildFromSource=true, DOTNET_BUILD_FROM_SOURCE; upstream excludes optional SQLite cache'))
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(records,indent=2)+'\n')
print('Roslyn Workspaces: source checksums/licenses, dependencies, complete managed/satellite payload, source lists, expanded friends, 1898-entry API baseline and 98/3780 resource manifest PASS')
