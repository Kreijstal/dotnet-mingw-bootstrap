"""Match the core/CIM consumer DLLs to rebuilt project outputs or installed source packages."""
import os
from pathlib import Path
import hashlib,json,sys,zipfile
root=Path(sys.argv[1]).resolve();consumer=Path(sys.argv[2]).resolve();source=root/'PowerShell-7.4.20'
project_paths={
 'System.Management.Automation.dll':source/'src/System.Management.Automation/bin/Release/net8.0/System.Management.Automation.dll',
 'Microsoft.PowerShell.CoreCLR.Eventing.dll':source/'src/Microsoft.PowerShell.CoreCLR.Eventing/bin/Release/net8.0/Microsoft.PowerShell.CoreCLR.Eventing.dll',
 'Microsoft.Management.Infrastructure.CimCmdlets.dll':source/'src/Microsoft.Management.Infrastructure.CimCmdlets/bin/Release/net8.0/Microsoft.Management.Infrastructure.CimCmdlets.dll'}
files={p.relative_to(consumer/'bin/Release/net8.0').as_posix():p for p in (consumer/'bin/Release/net8.0').rglob('*.dll') if p.name!='Check.dll'}
by_hash={}
for path in Path(os.environ['MINGW_PREFIX']+'/lib/dotnet/library-packs').glob('*.nupkg'):
 with zipfile.ZipFile(path) as z:
  for name in z.namelist():
   if name.endswith('.dll'):
    sha=hashlib.sha256(z.read(name)).hexdigest();by_hash.setdefault(sha,[]).append(dict(package=path.name,asset=name))
record={}
for relative,path in sorted(files.items()):
 sha=hashlib.sha256(path.read_bytes()).hexdigest()
 if path.name in project_paths:
  assert path.read_bytes()==project_paths[path.name].read_bytes(),relative
  record[relative]=dict(sha256=sha,project_source=project_paths[path.name].relative_to(source).as_posix())
 else:
  assert sha in by_hash,('No installed source-built package payload for consumer',relative,sha)
  record[relative]=dict(sha256=sha,installed_source_packages=by_hash[sha])
assert len(record)>=20,len(record)
(root/(consumer.name+'-payload.json')).write_text(json.dumps(record,indent=2)+'\n')
print('PASS every one of',len(record),'consumer DLLs matches rebuilt project bytes or an installed source-built NuGet payload')
