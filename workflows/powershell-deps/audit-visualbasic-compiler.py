"""Audit the source compiler host and byte-identical separately built dependencies."""
from pathlib import Path
import hashlib,io,json,subprocess,sys,zipfile
root=Path('mingw-w64-dotnet-visualbasic-compiler');fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
 key,sep,value=line.strip().partition(' = ')
 if sep:fields.setdefault(key,[]).append(value)
assert fields['pkgver']==['4.9.2'] and fields['pkgrel']==['1']
assert len(fields['source'])==len(fields['sha256sums'])
assert fields['sha256sums'][0]=='e01ea67aaec6408de51a0a4c18a7e15b2a527ca7a853c9a0a7742cb5680aeb30'
for source,wanted in zip(fields['source'],fields['sha256sums']):
 assert wanted!='SKIP'
 if '::' not in source:assert hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
artifacts=list(root.glob('*.pkg.tar.zst'));assert len(artifacts)==1;artifact=artifacts[0]
entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
info=payload('.PKGINFO').decode();assert 'pkgver = 4.9.2-1\n' in info
assert sorted(e[9:] for e in info.splitlines() if e.startswith('depend = '))==sorted(['mingw-w64-clang-x86_64-dotnet-runtime=8.0.131','mingw-w64-clang-x86_64-dotnet-codeanalysis-common=4.9.2','mingw-w64-clang-x86_64-dotnet-codeanalysis-visualbasic=4.9.2'])
assert 'clang64/share/licenses/dotnet-visualbasic-compiler/LICENSE' in entries
assert 'clang64/share/licenses/dotnet-visualbasic-compiler/THIRD-PARTY-NOTICES' in entries
assert hashlib.sha256(payload('clang64/share/licenses/dotnet-visualbasic-compiler/LICENSE')).hexdigest()=='ae48df11a335dc1a615f4f938b69cba73bcf4485c4f97af49b38efb0f216353b'
assert hashlib.sha256(payload('clang64/share/licenses/dotnet-visualbasic-compiler/THIRD-PARTY-NOTICES')).hexdigest()=='aa10edcdc488e201fdca979306ba0617de4c68c58dfa5916b25e588f5369fb82'
assert payload('clang64/share/doc/dotnet-visualbasic-compiler/README.md')==(root/'README.md').read_bytes()
sources=json.loads(payload('clang64/share/doc/dotnet-visualbasic-compiler/source-items.json'))
assert len(sources['vbc']['source_files'])==11 and len(sources['Microsoft.CodeAnalysis.ExternalAccess.RazorCompiler']['source_files'])==2
assert all('$(' not in d for s in sources.values() for d in s['friend_declarations'])
removed=json.loads(payload('clang64/share/doc/dotnet-visualbasic-compiler/removed-binaries.json'));assert removed and all(len(s['sha256'])==64 for s in removed)
base='clang64/lib/dotnet/compiler-packs/roslyn-vb-4.9.2/'
assert payload(base+'vbc49.cmd')==(root/'vbc49.cmd').read_bytes() and b'E:' not in payload(base+'vbc49.cmd')
config=json.loads(payload(base+'vbc.runtimeconfig.json'))
assert config['runtimeOptions']['framework']=={'name':'Microsoft.NETCore.App','version':'8.0.0'}
assert not any(e.endswith(('.pdb','.snk','.nupkg')) for e in entries)
assert not any('/sdk/' in e or e.startswith('clang64/bin/') for e in entries)
cultures=['cs','de','es','fr','it','ja','ko','pl','pt-BR','ru','tr','zh-Hans','zh-Hant']
expected=[base+n+'.dll' for n in ['vbc','Microsoft.CodeAnalysis','Microsoft.CodeAnalysis.VisualBasic','Microsoft.CodeAnalysis.ExternalAccess.RazorCompiler']]+[base+c+'/'+a+'.resources.dll' for c in cultures for a in ['Microsoft.CodeAnalysis','Microsoft.CodeAnalysis.VisualBasic']]
assert sorted(e for e in entries if e.endswith('.dll'))==sorted(expected)
deps=[]
for suffix,id,assembly in [('common','Microsoft.CodeAnalysis.Common','Microsoft.CodeAnalysis'),('visualbasic','Microsoft.CodeAnalysis.VisualBasic','Microsoft.CodeAnalysis.VisualBasic')]:
 package=next(Path('mingw-w64-dotnet-roslyn-visualbasic' if suffix=='visualbasic' else 'mingw-w64-dotnet-roslyn').glob('*-codeanalysis-'+suffix+'-4.9.2-*.pkg.tar.zst'))
 data=subprocess.check_output(['tar','--zstd','-xOf',str(package),'clang64/lib/dotnet/library-packs/'+id.lower()+'.4.9.2.nupkg'])
 with zipfile.ZipFile(io.BytesIO(data)) as z:
  for entry in z.namelist():
   if entry.endswith('.dll'):
    relative=entry.removeprefix('lib/net8.0/');assert payload(base+relative)==z.read(entry),relative
 deps.append(dict(package=id,version='4.9.2',managed_sha256=hashlib.sha256(payload(base+assembly+'.dll')).hexdigest(),satellite_cultures=cultures))
record=dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),source_commit='9934fb9e3527e1c0c51314e57d4aab30f97e8f9e',source_sha256=fields['sha256sums'][0],compiler='VisualBasic vbc4.9.2 net8.0',compiler_sha256=hashlib.sha256(payload(base+'vbc.dll')).hexdigest(),razor_helper_sha256=hashlib.sha256(payload(base+'Microsoft.CodeAnalysis.ExternalAccess.RazorCompiler.dll')).hexdigest(),source_counts={k:len(v['source_files']) for k,v in sources.items()},dependencies=deps,scope='standalone Visual Basic command-line host; existing SDK unchanged, VBCSCompiler/full Toolset/IDE not included')
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(record,indent=2)+'\n')
print('VisualBasic compiler49: exact pinned source/checksums/licenses, complete driver/helper/satellites, relative launcher, runtime config and byte-identical source-built compiler dependencies PASS')
