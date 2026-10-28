"""Measure old CoreCLR patch applicability in disposable source, without building."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
import tarfile

archive, root = (Path(p).resolve() for p in sys.argv[1:])
assert not root.exists(), root
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
assert digest == '1692acae5e3adfd625452fbc9db4fd4e982c10e38189358ae245c74dd5514d7a'
here = Path(__file__).resolve().parent
patches = [here / 'host-mingw.patch',
           *sorted((here.parent / 'runtime-servicing/patches').glob('*.patch'))[8:]]
assert len(patches) == 10
paths = {name for patch in patches
         for name in re.findall(r'^--- a/(.*)', patch.read_text(), re.M)}
introduced = {name for patch in patches
              for name in re.findall(r'^--- /dev/null\n\+\+\+ b/(.*)', patch.read_text(), re.M)}
root.mkdir(parents=True)
original = {}
with tarfile.open(archive) as tar:
    assert tar.pax_headers['comment'] == '081d220c0a773ffb7c6bea6b48727833576a65ef'
    for member in tar:
        prefix, _, name = member.name.partition('/')
        if name not in paths:
            continue
        assert prefix == 'runtime-10.0.5' and member.isfile()
        assert '..' not in Path(name).parts and not Path(name).is_absolute()
        data = tar.extractfile(member).read()
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        original[name] = hashlib.sha256(data).hexdigest()
records = []
for patch in patches:
    result = subprocess.run(['patch', '--forward', '--batch', '--fuzz=0',
                             '--no-backup-if-mismatch', '-p1', '-i', str(patch)],
                            cwd=root, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    (root / (patch.stem + '.log')).write_text(result.stdout)
    records.append(dict(patch=patch.name,
                        patch_sha256=hashlib.sha256(patch.read_bytes()).hexdigest(),
                        exit_code=result.returncode,
                        failed_hunks=len(re.findall('FAILED at', result.stdout)),
                        missing_files=len(re.findall("can't find file to patch", result.stdout))))
assert records[0]['exit_code'] == 0, records[0]
record = dict(source_version='10.0.5', runtime_archive_sha256=digest,
              scratch_root=str(root), original_files_sha256=original, patches=records,
              assessment='Mechanical applicability only: partial scratch application is not a usable CoreCLR10 port.',
              host_patch_applied_cleanly=True,
              coreclr_patch_failed_hunks=sum(p['failed_hunks'] for p in records[1:]),
              missing_original_paths=sorted(paths - original.keys() - introduced),
              introduced_port_files=sorted(introduced),
              coreclr10_built=False, full_dependency_goal_complete=False)
(root / 'review.json').write_text(json.dumps(record, indent=2) + '\n')
print('Review complete:', record['coreclr_patch_failed_hunks'],
      'rejected CoreCLR hunks;', len(record['missing_original_paths']),
      'missing original paths; no CoreCLR10 build claimed')
