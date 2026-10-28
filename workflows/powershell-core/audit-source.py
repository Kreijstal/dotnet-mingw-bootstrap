"""Verify original Windows source coverage and source-only PowerShell build inputs."""
from pathlib import Path
import hashlib,json,re,subprocess,sys,tarfile,xml.etree.ElementTree as E
root=Path(sys.argv[1]).resolve();source=root/'PowerShell-7.4.20'
archive=root/'powershell-7.4.20.tar.gz';assert hashlib.sha256(archive.read_bytes()).hexdigest()=='3dac352752acd64a0502c1f002ecf65eca177d2919bea3bcefbf8856afedd9eb'
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip()
base=native(source)+'/'
def relative(path):
 path=path.replace('\\','/');assert path.startswith(base),path;return path[len(base):]
def glob(pattern):
 parts=[];i=0
 while i<len(pattern):
  if pattern.startswith('**/',i):parts.append('(?:.*/)?');i+=3
  elif pattern.startswith('**',i):parts.append('.*');i+=2
  elif pattern[i]=='*':parts.append('[^/]*');i+=1
  elif pattern[i]=='?':parts.append('[^/]');i+=1
  else:parts.append(re.escape(pattern[i]));i+=1
 return re.compile('^'+''.join(parts)+'$',re.I)
projects={'System.Management.Automation':'src/System.Management.Automation','Microsoft.PowerShell.CoreCLR.Eventing':'src/Microsoft.PowerShell.CoreCLR.Eventing','PSVersionInfoGenerator':'src/System.Management.Automation/SourceGenerators/PSVersionInfoGenerator','Microsoft.Management.Infrastructure.CimCmdlets':'src/Microsoft.Management.Infrastructure.CimCmdlets','ResGen':'src/ResGen','TypeCatalogGen':'src/TypeCatalogGen'}
result={}
with tarfile.open(archive) as t:
 prefix='PowerShell-7.4.20/';members=t.getmembers();originals={p.name.removeprefix(prefix) for p in members if p.name.startswith(prefix)}
 source_bytes={p.name.removeprefix(prefix):t.extractfile(p).read() for p in members if p.isfile() and p.name.startswith(prefix) and p.name.endswith(('.cs','.csproj'))}
 for name,folder in projects.items():
  compile_file=root/'manifests'/(name+'-compile.txt')
  assert compile_file.is_file(),('Missing required project compile inventory',name)
  original_project=next(p for p in originals if p.startswith(folder+'/') and p.rsplit('/',1)[0]==folder and p.endswith('.csproj'))
  original_tree=E.fromstring(source_bytes[original_project])
  wanted={p for p in originals if p.startswith(folder+'/') and p.endswith('.cs')};excluded=[]
  for group in original_tree.findall('ItemGroup'):
   condition=group.get('Condition','').strip()
   if condition=="'$(IsWindows)' != 'true'":continue
   assert condition in {'',"'$(IsWindows)' == 'true'"},condition
   for item in group.findall('Compile'):
    if item.get('Include'):raise AssertionError('New original explicit Compile input needs an audit: '+item.get('Include'))
    if item.get('Remove'):
     pattern=glob(folder+'/'+item.get('Remove').replace('\\','/'));excluded.extend(p for p in wanted if pattern.match(p));wanted={p for p in wanted if not pattern.match(p)}
  files=[relative(line) for line in compile_file.read_text(encoding='utf-8-sig').splitlines() if line]
  actual={p for p in files if p in originals}
  assert wanted==actual,(name,sorted(wanted-actual),sorted(actual-wanted))
  for path in actual:assert (source/path).read_bytes()==source_bytes[path],path
  generated=[p for p in files if p not in originals]
  assert not any('/gen/SourceGenerated/' in p for p in files),'Persisted generated inputs must be regenerated, never duplicate compilation'
  result[name]=dict(original_sources=len(actual),generated_sources=len(generated),excluded_original_sources=sorted(set(excluded)),source_paths=sorted(actual),source_sha256={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in sorted(actual)},generated_paths=generated)
  analyzers=(root/'manifests'/(name+'-analyzers.txt')).read_text(encoding='utf-8-sig').splitlines()
  result[name]['analyzer_paths']=analyzers
  if name in {'System.Management.Automation','Microsoft.PowerShell.CoreCLR.Eventing','Microsoft.Management.Infrastructure.CimCmdlets'}:
   assert any('stylecop.analyzers.unstable' in p.lower() for p in analyzers),name
   assert any('dotnetanalyzers.documentationanalyzers.unstable' in p.lower() for p in analyzers),name
   style=[p for p in analyzers if '/codestyle/cs/' in p.replace('\\','/').lower()]
   assert len(style)==4 and all('sdk/8.0.131/' in p.replace('\\','/').lower() for p in style),(name,style)
   net=[p for p in analyzers if '/sdk/8.0.131/sdks/microsoft.net.sdk/analyzers/' in p.replace('\\','/').lower()]
   assert len(net)==2 and {Path(p.replace('\\','/')).name for p in net}=={'Microsoft.CodeAnalysis.NetAnalyzers.dll','Microsoft.CodeAnalysis.CSharp.NetAnalyzers.dll'},(name,net)
  if name=='PSVersionInfoGenerator':assert any('microsoft.codeanalysis.analyzers' in p.lower() for p in analyzers)
(root/'source-coverage.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS exact original Windows source inventories/hashes, documented exclusions, fresh generators and retained explicit source analyzers:',{k:(v['original_sources'],v['generated_sources']) for k,v in result.items()})
