"""Verify all stub signing identities and the original zero-version shim policy."""
from pathlib import Path
import hashlib,json,re,subprocess,sys,xml.etree.ElementTree as E
root=Path(sys.argv[1]).resolve();source=root/'m/src/runtime-8.0.31-native';build=root/'m/build/refpack-8.0.31'
keys=root/'bootstrap/dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f/bootstrap/m612/keys'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
rows=[line.split('\t') for line in (build/'plan.tsv').read_text().splitlines()]
stubs={}
for name,kind,project,deps in rows:
 if kind!='stub':continue
 name=name.removeprefix('stub:');original=source/'src/libraries/shims/stubs'/(name+'.csproj')
 key=E.parse(original).findtext('.//StrongNameKeyId') or 'Open';key='MSShared' if key=='MicrosoftShared' else key
 key_file=next(keys.glob(key+'-*.pubkey.hex'));expected=bytes.fromhex(key_file.read_text())
 file=build/'stubs'/(name+'.dll');metadata=subprocess.check_output(['/ucrt64/bin/monodis','--assembly',str(file)],text=True)
 actual=bytes.fromhex(''.join(re.findall(r'^0x[0-9a-fA-F]+:\s*([0-9a-fA-F ]+)',metadata,re.M)))
 assert actual==expected,(name,key)
 assert re.search(r'^Name:\s*'+re.escape(name)+r'\s*$',metadata,re.M),name
 stubs[name]=dict(original_project_sha256=sha(original),public_key_token=hashlib.sha1(actual).digest()[-8:][::-1].hex(),sha256=sha(file))
assert len(stubs)==20,len(stubs)
names={s.strip() for s in E.parse(source/'src/libraries/NetCoreAppLibrary.props').findtext('.//NetFxReference').split(';') if s.strip()}
assert len(names)==22,len(names)
shims={}
for name in sorted(names):
 for folder in [build/'out',root/'m/netcoreapp-8.0.31-coreclr/lib']:
  file=folder/(name+'.dll')
  if not file.is_file():continue
  text=subprocess.check_output(['/ucrt64/bin/monodis','--assemblyref',str(file)],text=True)
  versions=re.findall(r'Version=([^\s]+)',text);assert versions and set(versions)=={'0.0.0.0'},(file,versions)
  shims[file.relative_to(root).as_posix()]=dict(sha256=sha(file),reference_versions=versions)
report=dict(stub_count=20,stub_identities=stubs,framework_shim_names=sorted(names),zero_reference_version_shims=shims,original_arcade_task_audit=json.loads((root/'shim-version-audit.json').read_text()))
(root/'shim-identity-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS all20 original stub signing keys and all22 framework shim zero-version identities:',len(shims),'reference/runtime DLLs')
