"""Collect actual source native-package owners and their complete license files."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('framework_root',type=Path);parser.add_argument('pack',type=Path);parser.add_argument('destination',type=Path)
args=parser.parse_args();frame=args.framework_root.resolve();pack=args.pack.resolve();out=args.destination.resolve();assert not out.exists(),out
manifest=json.loads((pack/'source-payload.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
archives={}
for item in manifest['files'].values():
 for owner in item['origin'].get('owners',[]):
  name=owner['package'];digest=owner['source_feed_archive_sha256']
  assert name not in archives or archives[name]==digest,name
  archives[name]=digest
assert len(archives)==11,len(archives)
out.mkdir();records={}
for archive,digest in sorted(archives.items()):
 path=Path('/clang64/lib/dotnet/library-packs')/archive;assert sha(path)==digest,path
 package=subprocess.check_output(['pacman','-Qqo',str(path)],text=True).strip()
 version=subprocess.check_output(['pacman','-Q',package],text=True).strip()
 paths=subprocess.check_output(['pacman','-Qlq',package],text=True).splitlines()
 licenses=[Path(p) for p in paths if '/share/licenses/' in p and not p.endswith('/')]
 assert licenses,(archive,package)
 owned={}
 for license in licenses:
  assert license.is_file() and license.stat().st_size>0,license
  # Keep the upstream/native-package directory and filename to avoid collisions.
  relative=license.relative_to('/clang64/share/licenses').as_posix();target=out/relative;target.parent.mkdir(parents=True,exist_ok=True)
  if target.exists():assert target.read_bytes()==license.read_bytes(),relative
  else:shutil.copy2(license,target)
  owned[relative]=dict(sha256=sha(license),bytes=license.stat().st_size,original_installed_path=str(license))
 records[archive]=dict(native_package=version,source_nuget_archive_sha256=digest,license_files=owned)
runtime=frame/'m/src/runtime-8.0.31-native'
for name in ['LICENSE.TXT','THIRD-PARTY-NOTICES.TXT']:
 target=out/'dotnet-runtime-source'/name;target.parent.mkdir(exist_ok=True);shutil.copy2(runtime/name,target)
report=dict(supporting_source_nuget_owners=records,runtime_source_licenses={name:sha(runtime/name) for name in ['LICENSE.TXT','THIRD-PARTY-NOTICES.TXT']},supporting_owner_count=len(records),supporting_license_files=sum(len(item['license_files']) for item in records.values()),installed_or_packaged=False)
(out/'source-owners.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS11 source NuGet/native-package owner mappings,',report['supporting_license_files'],'full native license/notice files and original runtime source licenses; no installed files changed')
