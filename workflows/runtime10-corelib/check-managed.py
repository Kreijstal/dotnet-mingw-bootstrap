"""Windows Python runner for source10 corerun/CoreLib; capture each actual result."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

out = Path(sys.argv[1]).resolve()
layout = out / 'native CLR10 λ'
assert (layout / 'ManagedProbe.dll').is_file()
env = dict(os.environ)
record = dict(native_layout=str(layout), cases={}, full_runtime10_built=False,
              full_sdk10_built=False, full_dependency_goal_complete=False)
winpath = lambda path: str(path).replace('/', '\\')
for name in ['basic', 'gc', 'exceptions', 'thread']:
    command = [winpath(layout / 'corerun.exe'), '-c', winpath(layout), winpath(layout / 'ManagedProbe.dll'), name]
    try:
        result = subprocess.run(command, env=env, cwd=layout, text=True,
                                encoding='utf-8', errors='replace', capture_output=True, timeout=60)
        (out / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        record['cases'][name] = dict(command=command, exit_code=result.returncode)
    except subprocess.TimeoutExpired as error:
        record['cases'][name] = dict(command=command, timeout_seconds=60)
        (out / 'results.json').write_text(json.dumps(record, indent=2) + '\n')
        raise
    (out / 'results.json').write_text(json.dumps(record, indent=2) + '\n')
    assert result.returncode == 0, (name, result.returncode, result.stdout, result.stderr)
    print('PASS source10 managed execution:', name, flush=True)
record['all_four_managed_cases_passed'] = True
record['tested_files_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in layout.iterdir() if p.is_file()}
(out / 'results.json').write_text(json.dumps(record, indent=2) + '\n')
