"""Reapply the declared port from pinned originals in an owned, idle build tree."""
from pathlib import Path
import hashlib,json,re,subprocess,sys,tarfile,tempfile
root=Path(sys.argv[1]).resolve();source=root/'r';here=Path(__file__).resolve().parent
patch=here/'host-mingw.patch';text=patch.read_text()
paths=re.findall(r'^--- a/(.+)$',text,re.M)
record=json.loads((root/'source-inputs.json').read_text());before={}
changed=[]
with tempfile.TemporaryDirectory(prefix='host-patch-',dir=root) as temporary:
    stage=Path(temporary)
    with tarfile.open(root/'runtime-v10.0.5.tar.gz') as tar:
        for name in paths:
            data=tar.extractfile('runtime-10.0.5/'+name).read()
            assert hashlib.sha256(data).hexdigest()==record['original_file_sha256'][name]
            before[name]=hashlib.sha256((source/name).read_bytes()).hexdigest()
            target=stage/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    result=subprocess.run(['patch','--fuzz=0','--no-backup-if-mismatch','-p1','-i',str(patch)],cwd=stage,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    assert result.returncode==0,result.stdout
    for name in paths:
        data=(stage/name).read_bytes()
        if (source/name).read_bytes()!=data:
            (source/name).write_bytes(data);changed.append(name)
record.setdefault('patch_history',[]).append(dict(previous_patch_sha256=record['host_patch_sha256'],previous_source_sha256=before))
record['host_patch_sha256']=hashlib.sha256(patch.read_bytes()).hexdigest()
record['host_patch_log']=result.stdout
(root/'source-inputs.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS host port refreshed from pinned originals with zero-fuzz application; changed files:',changed)
