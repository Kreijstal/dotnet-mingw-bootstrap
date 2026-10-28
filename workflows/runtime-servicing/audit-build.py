"""Audit serviced source/native/generator/CoreLib provenance; never imply full runtime completion."""
from pathlib import Path
import hashlib,json,posixpath,re,subprocess,sys,tarfile,tempfile
root=Path(sys.argv[1]).resolve();source=root/'m/src/runtime-8.0.31-native';record=json.loads((root/'source-inputs.json').read_text());sha=lambda b:hashlib.sha256(b).hexdigest()
archive=root/'m/src/runtime-8.0.31.tar.gz';assert sha(archive.read_bytes())==record['runtime_archive_sha256']=='b32a56a29cc0248bed219e1c7ef026bf8f006d997f5d6346382e63a22ecd9070'
base=root/'bootstrap/dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f';patchdir=base/'patches/m612/runtime-8.0.31'
changed=set()
for path in patchdir.glob('*.patch'):changed.update(re.findall(r'^\+\+\+ b/(.+)$',path.read_text(),re.M))
original={}
with tarfile.open(archive,'r|gz') as t:
 for m in t:
  if not m.isfile():continue
  path=m.name.split('/',1)[1]
  if path in changed:original[path]=t.extractfile(m).read()
with tempfile.TemporaryDirectory() as temp:
 replay=Path(temp)
 for path,data in original.items():
  target=replay/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
 for item in record['patches']:
  patch=patchdir/item['name'];assert sha(patch.read_bytes())==item['ported_sha256']
  assert sha((base/'patches/m612/runtime-8.0.0'/item['name']).read_bytes())==item['original_sha256']
  subprocess.run(['patch','--fuzz=0','--no-backup-if-mismatch','-p1','-i',str(patch)],cwd=replay,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 for path in changed:assert (source/path).read_bytes()==(replay/path).read_bytes(),path
for path,digest in record['original_file_sha256'].items():
 if path not in changed:assert sha((source/path).read_bytes())==digest,path
for item in record['removed_binary_inputs']:assert not (source/item['path']).exists()
for link in record['materialized_readme_links']:
 path=source/link['path'];assert path.read_bytes()==(path.parent/link['target']).read_bytes()
# Independently reconstruct native/CoreLib helper overlays, including the
# servicing file-version correction. All production source hashes above remain
# tied to the pinned runtime archive and the 17 explicit native patches.
version_inputs=json.loads((root/'version-build-inputs.json').read_text())
assert version_inputs['arcade_source_commit']=='444c2ef3c414558e2e409ee346f1faebb19687eb'
assert version_inputs['original_task_sha256']=='e0bc140b716fb1c43b4609e877a357b422cdd4b213021bd919df04e963c8a02f'
assert version_inputs['file_version']==(root/'file-version.txt').read_text().strip()=='8.0.3126.42000'
version_adapters={item['path']:item for item in json.loads((root/'file-version-adapters.json').read_text())}
corelib_adapter=json.loads((root/'corelib-build-adapter.json').read_text())
with tarfile.open(root/'m/src/dotnet-bootstrap-source.tar.gz') as archive:
 for name in ['corehost.sh','corelib.sh']:
  relative='dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f/bootstrap/m612/'+name
  expected=archive.extractfile(relative).read().decode()
  for pair in version_adapters[name]['replacements']:
   assert expected.count(pair['before'])==pair['count'],name
   expected=expected.replace(pair['before'],pair['after'])
  if name=='corelib.sh':
   assert sha(expected.encode())==corelib_adapter['original_sha256']
   for pair in corelib_adapter['replacements']:
    assert expected.count(pair['before'])==1
    expected=expected.replace(pair['before'],pair['after'])
  assert (base/'bootstrap/m612'/name).read_text()==expected,name
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip().replace('\\','/')
source_prefix=native(source)+'/'
managed=root/'managed-build';generators=json.loads((managed/'source-inputs.json').read_text());result={}
for name,item in generators.items():
 if name=='RuntimeBuildTools':continue
 folder=managed/name;actual=set();generated={}
 for line in (folder/'obj/Release/net8.0/Compile.txt').read_text(encoding='utf-8-sig').splitlines():
  line=line.replace('\\','/')
  if line.lower().startswith(source_prefix.lower()):
   path=line[len(source_prefix):];actual.add(path)
   assert sha((source/path).read_bytes())==record['original_file_sha256'][path]
  else:
   prefix=native(folder)+'/'
   assert line.lower().startswith(prefix.lower()),line
   path=line[len(prefix):]
   assert path in ['SR.Generated.cs','obj/Release/net8.0/Build.AssemblyInfo.cs','obj/Release/net8.0/.NETCoreApp,Version=v8.0.AssemblyAttributes.cs'],path
   generated[path]=sha((folder/path).read_bytes())
 expected=set(item['original_cs'])
 if name!='System.Private.CoreLib.Generators':expected.add('src/libraries/Common/src/System/SR.cs')
 assert actual==expected,(name,sorted(expected-actual),sorted(actual-expected))
 for path,digest in item['original_cs'].items():assert sha((source/path).read_bytes())==digest
 refs=[];cache=managed/'cache';prefix=native(cache)+'/'
 for line in (folder/'obj/Release/net8.0/ReferencePath.txt').read_text(encoding='utf-8-sig').splitlines():
  line=line.replace('\\','/')
  if line.lower().startswith(prefix.lower()):origin='source-package'
  elif '/packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0/' in line:origin='source-build-framework'
  elif '/managed-build/' in line and '/Microsoft.Interop.SourceGeneration/' in line:origin='new-source-project'
  else:raise AssertionError(line)
  refs.append(dict(origin=origin,path=line))
 dll=folder/'bin/Release/net8.0'/(name+'.dll');assert dll.is_file()
 result[name]=dict(original_compiler_inputs=len(actual),generated_inputs=generated,implementation_sha256=sha(dll.read_bytes()),references=refs)
# CoreLib's real csc response file retains all original and generated inputs.
rsp=root/'m/build/corelib-coreclr-8.0.31/obj/System.Private.CoreLib.rsp';selected={};generated={}
for value in rsp.read_text().splitlines():
 value=posixpath.normpath(re.sub('/+','/',value.strip('"').replace('\\','/')))
 if value.startswith('-') or not value.endswith('.cs'):continue
 if value.lower().startswith(source_prefix.lower()):
  path=value[len(source_prefix):];selected[path]=sha((source/path).read_bytes());assert selected[path]==record['original_file_sha256'][path]
 else:
  prefix=native(root)+'/'
  assert value.lower().startswith(prefix.lower()),value
  path=value[len(prefix):];generated[path]=sha((root/path).read_bytes())
assert len(selected)==1803,(len(selected),list(generated))
corelib=root/'m/netcoreapp-8.0.31-coreclr/lib/System.Private.CoreLib.dll';assert corelib.is_file()
outputs={}
for folder in [root/'m/coreclr-8.0.31',root/'m/corehost-8.0.31/corehost']:
 for file in folder.glob('*'):
  if file.is_file():outputs[file.relative_to(root).as_posix()]=sha(file.read_bytes())
assert len(outputs)>=10
report=dict(runtime_source_commit=record['runtime_source_commit'],runtime_archive_sha256=record['runtime_archive_sha256'],original_source_files_checked=len(record['original_file_sha256']),native_patch_files_replayed=17,patched_source_files=len(changed),generators=result,corelib_original_compiler_inputs=selected,corelib_generated_compiler_inputs=generated,corelib_sha256=sha(corelib.read_bytes()),native_output_sha256=outputs,full_serviced_runtime_completed=False)
report['version_build_inputs']=version_inputs
(root/'build-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS exact serviced source hashes,17 independent zero-fuzz patch replays, full generator compiler/reference inventories,1803 CoreLib inputs/generated metadata and native/CoreLib output hashes; complete serviced framework/integration remain open')
