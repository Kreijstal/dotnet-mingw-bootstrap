"""Prepare an isolated, pinned .NET10 native host source tree."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys,tarfile

archive,root=(Path(p).resolve() for p in sys.argv[1:]);source=root/'r'
assert not source.exists();source.mkdir(parents=True)
sha=lambda data:hashlib.sha256(data).hexdigest()
assert sha(archive.read_bytes())=='1692acae5e3adfd625452fbc9db4fd4e982c10e38189358ae245c74dd5514d7a'
original={};removed=[];links=[];directories={source}
with tarfile.open(archive) as tar:
    assert tar.pax_headers['comment']=='081d220c0a773ffb7c6bea6b48727833576a65ef'
    for member in tar:
        if member.isdir():continue
        prefix,name=member.name.split('/',1)
        assert prefix=='runtime-10.0.5' and '..' not in Path(name).parts and not Path(name).is_absolute()
        if member.issym():links.append(dict(path=name,target=member.linkname));continue
        assert member.isfile(),member.name
        data=tar.extractfile(member).read()
        binary=(Path(name).suffix.lower() in {'.dll','.exe','.so','.dylib','.nupkg','.a','.lib','.o','.obj','.winmd','.pdb','.resources','.wasm','.bc'}
                or data[:2]==b'MZ' or data[:4] in {b'\x7fELF',b'\x00asm',b'BC\xc0\xde',b'\xcf\xfa\xed\xfe',b'\xfe\xed\xfa\xcf'})
        if binary:removed.append(dict(path=name,sha256=sha(data)));continue
        path=source/name
        if path.parent not in directories:
            path.parent.mkdir(parents=True,exist_ok=True);directories.add(path.parent)
        path.write_bytes(data)
        original[name]=sha(data)
for link in links:
    path=source/link['path'];target=(path.parent/link['target']).resolve()
    assert target.is_relative_to(source) and target.is_file(),link
    path.write_bytes(target.read_bytes())
here=Path(__file__).resolve().parent;patch=here/'host-mingw.patch'
result=subprocess.run(['patch','--fuzz=0','--no-backup-if-mismatch','-p1','-i',str(patch)],cwd=source,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
assert result.returncode==0,result.stdout
(root/'source-inputs.json').write_text(json.dumps(dict(runtime_version='10.0.5',runtime_source_commit='081d220c0a773ffb7c6bea6b48727833576a65ef',runtime_archive_sha256=sha(archive.read_bytes()),original_file_sha256=original,removed_binary_inputs=removed,materialized_links=links,host_patch_sha256=sha(patch.read_bytes()),host_patch_log=result.stdout,full_runtime10_built=False,full_sdk10_built=False),indent=2)+'\n')
print('PASS pinned10.0.5 source preparation:',len(original),'original inputs,',len(removed),'prebuilt inputs removed; zero-fuzz host patch applied')
