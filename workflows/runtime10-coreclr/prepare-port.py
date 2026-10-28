"""Apply the standalone CoreCLR10 candidate after pinned native host preparation."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
here = Path(__file__).resolve().parent
inputs = json.loads((root / 'source-inputs.json').read_text())
assert inputs['runtime_source_commit'] == '081d220c0a773ffb7c6bea6b48727833576a65ef'
record = json.loads((here / 'port-source-inputs.json').read_text())
patch = here / 'coreclr-mingw.patch'
assert hashlib.sha256(patch.read_bytes()).hexdigest() == record['patch_sha256']
for name, item in record['changed_files'].items():
    path = root / 'r' / name
    if item['original_sha256'] is None:
        assert not path.exists(), name
    else:
        assert hashlib.sha256(path.read_bytes()).hexdigest() == item['original_sha256'], name
result = subprocess.run(['patch', '--forward', '--fuzz=0', '--no-backup-if-mismatch',
                         '-p1', '-i', str(patch)], cwd=root / 'r', text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
assert result.returncode == 0, result.stdout
for name, item in record['changed_files'].items():
    assert hashlib.sha256((root / 'r' / name).read_bytes()).hexdigest() == item['ported_sha256'], name
record['applied_patch_log'] = result.stdout
record['bootstrap_limitations'] = [
    'DAC/out-of-process debugger are not built yet',
    'Source cDAC tool is bootstrapped on net8; SDK10 tool execution remains open',
    'ETW provider registration remains open; EventPipe implementation is retained',
    'Existing GNU/Windows EHa shim limitations still require execution coverage',
    'Managed runtime10 and SDK10 are not built yet']
(root / 'coreclr-port-inputs.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS pinned original inputs and standalone zero-fuzz CoreCLR10 port:',
      len(record['changed_files']), 'changed files')
