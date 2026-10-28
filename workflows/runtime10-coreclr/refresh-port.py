"""Refresh a stopped private prototype from pinned source, preserving unchanged mtimes."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tarfile
import tempfile

root = Path(sys.argv[1]).resolve()
here = Path(__file__).resolve().parent
archive = root / 'runtime-v10.0.5.tar.gz'
sha = lambda data: hashlib.sha256(data).hexdigest()
assert sha(archive.read_bytes()) == '1692acae5e3adfd625452fbc9db4fd4e982c10e38189358ae245c74dd5514d7a'
record = json.loads((here / 'port-source-inputs.json').read_text())
patch = here / 'coreclr-mingw.patch'
assert sha(patch.read_bytes()) == record['patch_sha256']
with tempfile.TemporaryDirectory(prefix='port-refresh-', dir=root) as folder:
    stage = Path(folder)
    with tarfile.open(archive) as tar:
        assert tar.pax_headers['comment'] == '081d220c0a773ffb7c6bea6b48727833576a65ef'
        for member in tar:
            prefix, _, name = member.name.partition('/')
            if name not in record['changed_files']:
                continue
            assert prefix == 'runtime-10.0.5' and member.isfile()
            assert '..' not in Path(name).parts and not Path(name).is_absolute()
            item = record['changed_files'][name]
            data = tar.extractfile(member).read()
            assert sha(data) == item['original_sha256'], name
            target = stage / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    result = subprocess.run(['patch', '--forward', '--fuzz=0', '--no-backup-if-mismatch',
                             '-p1', '-i', str(patch)], cwd=stage, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert result.returncode == 0, result.stdout
    changed = []
    for name, item in record['changed_files'].items():
        prepared = stage / name
        assert sha(prepared.read_bytes()) == item['ported_sha256'], name
        target = root / 'r' / name
        assert target.resolve().is_relative_to(root / 'r'), name
        if not target.exists() or target.read_bytes() != prepared.read_bytes():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(prepared, target)
            changed.append(name)
    history_path = root / 'coreclr-port-refreshes.json'
    history = json.loads(history_path.read_text()) if history_path.exists() else []
    history.append(dict(patch_sha256=record['patch_sha256'], changed_files=changed,
                        clean_apply_exit_code=0, clean_apply_log=result.stdout))
    history_path.write_text(json.dumps(history, indent=2) + '\n')
(root / 'coreclr-current-port-inputs.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS pinned-source zero-fuzz refresh:', len(changed), 'files changed; other mtimes retained')
