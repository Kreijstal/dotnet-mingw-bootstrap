"""Stage a separately installed native .NET10 bootstrap component."""
from pathlib import Path
import hashlib,json,shutil,sys
root,prefix,readme=(Path(p).resolve() for p in sys.argv[1:]);assert not prefix.exists()
native=prefix/'lib/dotnet10-bootstrap'
shutil.copytree(root/'host/corehost',native)
(native/'host/fxr/10.0.5').mkdir(parents=True)
shutil.copy2(native/'hostfxr.dll',native/'host/fxr/10.0.5/hostfxr.dll')
doc=prefix/'share/doc/dotnet-host10-bootstrap';doc.mkdir(parents=True)
for name in ['source-inputs.json','version-build-inputs.json','native-version-inputs.json','host-audit.json']:
    shutil.copy2(root/name,doc/name)
here=Path(__file__).resolve().parent
shutil.copy2(here/'check-native-host.py',doc/'check-native-host.py')
shutil.copy2(readme,doc/'README.md')
licenses=prefix/'share/licenses/dotnet-host10-bootstrap';licenses.mkdir(parents=True)
for name in ['LICENSE.TXT','THIRD-PARTY-NOTICES.TXT']:shutil.copy2(root/'r'/name,licenses/name)
shutil.copy2(here/'inputs/Arcade-LICENSE.TXT',licenses/'Arcade-LICENSE.TXT')
files={p.relative_to(prefix).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in prefix.rglob('*') if p.is_file()}
(doc/'source-payload.json').write_text(json.dumps(dict(files_sha256=files,native_source_version='10.0.5',full_runtime10_built=False,full_sdk10_built=False),indent=2)+'\n')
print('PASS isolated native host component staged; existing dotnet8 files are not package targets')
