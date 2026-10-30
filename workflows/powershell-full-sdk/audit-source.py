"""Check complete original console/LocalAccounts inputs alongside core projects."""
from pathlib import Path
import hashlib,json,runpy,subprocess,sys,tarfile,xml.etree.ElementTree as E
root=Path(sys.argv[1]).resolve();source=root/'PowerShell-7.4.20';here=Path(__file__).resolve().parent
import os
shared=Path(os.environ['POWERSHELL_PACKAGE_DIR'])/'module-host-audit-source.py' if 'POWERSHELL_PACKAGE_DIR' in os.environ else here.parent.parent/'mingw-w64-powershell-module-test-host/audit-source.py'
runpy.run_path(str(shared),run_name='__main__')
report=json.loads((root/'source-coverage.json').read_text());native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip().replace('\\','/')
prefix=native(source)+'/'
overlay=runpy.run_path(str(here/'localaccounts-overlay.py'))
with tarfile.open(root/'powershell-7.4.20.tar.gz') as tar:
 original={p.name.removeprefix('PowerShell-7.4.20/'):tar.extractfile(p).read() for p in tar if p.isfile() and p.name.endswith('.cs')}
 expected=overlay['adapted_sources'](original)
 declared=json.loads((root/'localaccounts-source-overlay.json').read_text())
 assert declared['helper_sha256']==hashlib.sha256((here/'localaccounts-overlay.py').read_bytes()).hexdigest()
 assert declared['removed_original_sources']==[] and declared['analyzers_disabled'] is False
 assert declared['changed_sources']=={p:dict(original_sha256=hashlib.sha256(original[p]).hexdigest(),adapted_sha256=hashlib.sha256(expected[p]).hexdigest()) for p in original if original[p]!=expected[p]}
 for name,folder,wanted in [
  ('Microsoft.PowerShell.LocalAccounts','src/Microsoft.PowerShell.LocalAccounts',None),
  ('pwsh','src/powershell-win-core',{'src/powershell/Program.cs'}),
  ('Microsoft.PowerShell.SDK','src/Microsoft.PowerShell.SDK',set())]:
  if wanted is None:wanted={p for p in original if p.startswith(folder+'/')}
  manifest=root/'manifests'/((name if name!='pwsh' else 'powershell-win-core')+'-compile.txt')
  if name=='pwsh' and not manifest.exists():manifest=root/'manifests/pwsh-compile.txt'
  lines=manifest.read_text(encoding='utf-8-sig').splitlines();selected=[]
  for line in lines:
   line=line.replace('\\','/');assert line.lower().startswith(prefix.lower()),line;selected.append(line[len(prefix):])
  actual={p for p in selected if p in original};assert actual==wanted,(name,sorted(wanted-actual),sorted(actual-wanted))
  for p in actual:assert (source/p).read_bytes()==expected[p],p
  report[name]=dict(original_sources=len(actual),source_sha256={p:hashlib.sha256(original[p]).hexdigest() for p in sorted(actual)},generated_source_paths=[p for p in selected if p not in original])
  report[name]['explicit_source_overlays']={p:dict(original_sha256=hashlib.sha256(original[p]).hexdigest(),adapted_sha256=hashlib.sha256(expected[p]).hexdigest()) for p in sorted(actual) if original[p]!=expected[p]}
  analyzer_name='powershell-win-core' if name=='pwsh' and (root/'manifests/powershell-win-core-analyzers.txt').exists() else name
  analyzers=(root/'manifests'/(analyzer_name+'-analyzers.txt')).read_text(encoding='utf-8-sig').splitlines() if name!='Microsoft.PowerShell.SDK' else []
  if name in ['pwsh','Microsoft.PowerShell.LocalAccounts']:
   for token in ['stylecop.analyzers.unstable','dotnetanalyzers.documentationanalyzers.unstable','/codestyle/cs/','Microsoft.CodeAnalysis.CSharp.NetAnalyzers.dll']:
    assert any(token.lower() in line.replace('\\','/').lower() for line in analyzers),(name,token)
   style=[p for p in analyzers if '/codestyle/cs/' in p.replace('\\','/').lower()]
   net=[p for p in analyzers if '/sdk/8.0.131/sdks/microsoft.net.sdk/analyzers/' in p.replace('\\','/').lower()]
   assert len(style)==4 and all('sdk/8.0.131/' in p.replace('\\','/').lower() for p in style),(name,style)
   assert len(net)==2 and {Path(p.replace('\\','/')).name for p in net}=={'Microsoft.CodeAnalysis.NetAnalyzers.dll','Microsoft.CodeAnalysis.CSharp.NetAnalyzers.dll'},(name,net)
(root/'full-source-coverage.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS original full SDK/core/console/LocalAccounts source hashes, generator paths and source analyzer coverage:',{name:item['original_sources'] for name,item in report.items()})
