"""Create the relocatable application, native entry point and license closure."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import sys

root, app, licenses, launcher, launcher_license, prefix = (Path(p).resolve() for p in sys.argv[1:])
assert not prefix.exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
shutil.copytree(app,prefix/'lib/powershell/7')
(prefix/'bin').mkdir()
shutil.copy2(launcher,prefix/'bin/pwsh.exe')
shutil.copytree(licenses,prefix/'share/licenses/powershell')
shutil.copy2(launcher_license,prefix/'share/licenses/powershell/LICENSE.launcher')
doc=prefix/'share/doc/powershell';doc.mkdir(parents=True)
here=Path(__file__).resolve().parent
package=Path(os.environ.get('POWERSHELL_PACKAGE_DIR',here.parents[1]/'mingw-w64-powershell'))
desktop=os.environ.get('POWERSHELL_DESKTOP','1')!='0'
for name in ['README.md','launcher.c']:
    shutil.copy2(package/name,doc/name)
checks=doc/'checks';checks.mkdir()
for name in ['check-package.sh','check-launcher.py']:
    shutil.copy2(package/name,checks/name)
for name in ['Smoke.ps1','CimWmiSmoke.ps1','WmiStaProbe.ps1','GallerySmoke.ps1','ChildSmoke.ps1']+['DesktopSmoke.ps1']*desktop:
    shutil.copy2(here/name,checks/name)
names=['source-build-inputs.json','localaccounts-source-overlay.json','localaccounts-source-overlay.diff',
       'full-source-coverage.json']+['graphicalhost-source-audit.json']*desktop+['full-sdk-catalog-audit.json','native-console-source.json']
for name in names:shutil.copy2(root/name,doc/name)
shutil.copy2(root/(app.name+'-payload-audit.json'),doc/'application-source-payload.json')
shutil.copy2(root/(app.name+'-license-closure.json'),doc/'license-closure.json')
files={p.relative_to(prefix).as_posix():sha(p) for p in prefix.rglob('*') if p.is_file()}
manifest=dict(source_version='7.4.20',source_archive_sha256='3dac352752acd64a0502c1f002ecf65eca177d2919bea3bcefbf8856afedd9eb',
    application_home='lib/powershell/7',native_entry='bin/pwsh.exe',
    native_launcher_sha256=sha(launcher),source_launcher_c_sha256=sha(package/'launcher.c'),files_sha256=files,
    bundled_application_files=len([p for p in files if p.startswith('lib/powershell/7/')]),
    license_files=len([p for p in files if p.startswith('share/licenses/')]),
    full_dependency_goal_complete=False)
(doc/'source-package.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('PASS staged native PowerShell distribution:',manifest['bundled_application_files'],'application files,',manifest['license_files'],'licenses and complete source/package provenance')
