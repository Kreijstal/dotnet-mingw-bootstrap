"""Compare the replacement SDK native artifact to its tested source stage."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tarfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('artifact',type=Path)
parser.add_argument('stage',type=Path)
parser.add_argument('--output',type=Path)
args=parser.parse_args();stage=args.stage.resolve();src=stage.parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((stage/'source-sdk-servicing.json').read_text())
for path,digest in manifest['unchanged_seed_files'].items():assert sha(stage/path)==digest,path
adapt=manifest['sdk_metadata_adaptation'];assert sha(stage/adapt['path'])==adapt['adapted_sha256']
expected={p.relative_to(stage).as_posix():sha(p) for p in (stage/'clang64').rglob('*') if p.is_file()}
doc='clang64/share/doc/dotnet-sdk8-servicing/'
expected[doc+'README.md']=sha(src/'README.md')
expected[doc+'source-sdk-servicing.json']=sha(stage/'source-sdk-servicing.json')
for name in ['source-inputs','version-build-inputs','file-version-adapters']:
 expected[doc+'host-'+name+'.json']=sha(src/'prepared'/(name+'.json'))
for name in ['check-packs.sh','audit-packs-check.py']:expected[doc+name]=sha(src/'sdk-servicing'/name)
expected[doc+'runtime-list-source-inputs.json']=sha(src/'sdk-servicing/inputs/source-inputs.json')
license_prefix='clang64/share/licenses/dotnet-sdk8-servicing/'
for name in ['LICENSE.TXT','THIRD-PARTY-NOTICES.TXT']:
 expected[license_prefix+'runtime-'+name]=sha(src/'prepared/m/src/runtime-8.0.31-native'/name)
expected[license_prefix+'Arcade-LICENSE.TXT']=sha(src/'sdk-servicing/inputs/LICENSE.TXT')
actual={};fields={}
with subprocess.Popen(['zstd','-dc',str(args.artifact)],stdout=subprocess.PIPE) as process:
 with tarfile.open(fileobj=process.stdout,mode='r|') as tar:
  for member in tar:
   if member.isdir():continue
   assert member.isfile(),member.name
   data=tar.extractfile(member).read()
   if member.name=='.PKGINFO':
    for line in data.decode().splitlines():
     key,sep,value=line.partition(' = ')
     if sep:fields.setdefault(key,[]).append(value)
   elif member.name.startswith('.'):assert member.name in {'.BUILDINFO','.MTREE'},member.name
   else:actual[member.name]=hashlib.sha256(data).hexdigest()
 while process.stdout.read(1024*1024):pass
 assert process.wait()==0
assert fields['pkgname']==['mingw-w64-clang-x86_64-dotnet-sdk8-servicing']
assert fields['pkgver']==['8.0.131-3']
assert fields['provides']==['mingw-w64-clang-x86_64-dotnet-sdk=8.0.131-3']
assert fields['conflict']==['mingw-w64-clang-x86_64-dotnet-sdk']
assert set(fields['depend'])=={'mingw-w64-clang-x86_64-dotnet-runtime=8.0.131','mingw-w64-clang-x86_64-dotnet-runtime8-servicing=8.0.31-3','mingw-w64-clang-x86_64-dotnet-targeting8-servicing>=8.0.31-2','mingw-w64-clang-x86_64-dotnet-sdk-netanalyzers=8.0.0.preview26274.2'}
assert actual==expected,(sorted(set(expected)-set(actual)),sorted(set(actual)-set(expected)),[p for p in set(actual)&set(expected) if actual[p]!=expected[p]])
result=dict(artifact=args.artifact.name,artifact_sha256=sha(args.artifact),source_seed_sha256=manifest['sdk_source_seed_sha256'],unchanged_seed_files=len(manifest['unchanged_seed_files']),fresh_host_pack_files=len(manifest['replaced_host_files']),serviced_runtime_pack_files=175,files_sha256=actual,source_stage_bytes_match=True,fresh_install_verified=False,full_dependency_goal_complete=False)
if args.output:args.output.write_text(json.dumps(result,indent=2)+'\n')
print('PASS exact unchanged source SDK files, one metadata adaptation, fresh native host/runtime pack bytes, original licenses and replacement SDK ownership/dependencies')
