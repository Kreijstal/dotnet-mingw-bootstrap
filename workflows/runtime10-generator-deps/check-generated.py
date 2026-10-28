#!/usr/bin/env python3
"""Require every regenerated compiler file to match its checked-in baseline."""
from pathlib import Path
import hashlib
import json
import sys

source, generated = map(Path, sys.argv[1:])
baseline = source / 'src/Compilers/CSharp/Portable/Generated'
originals = {p.name: p for p in baseline.rglob('*.cs')}
actual = {p.name: p for p in generated.glob('*.cs')}
assert actual.keys() == originals.keys(), (actual.keys(), originals.keys())
records = []
for name, path in sorted(actual.items()):
    original = originals[name]
    # The console entry point removes blank lines immediately after '{'; the
    # incremental generator does not. Apply exactly that upstream cleanup.
    def normalized(p):
        text = p.read_text(encoding='utf-8-sig').replace('\r\n', '\n').strip()
        while '{\n\n' in text:
            text = text.replace('{\n\n', '{\n')
        return text
    assert normalized(path) == normalized(original), name
    records.append({'file': name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                    'upstream_sha256': hashlib.sha256(original.read_bytes()).hexdigest(),
                    'equivalent_after_bom_line_endings_and_console_blank_line_cleanup': True})
(generated.parent / 'generated-sources.json').write_text(json.dumps(records, indent=2) + '\n')
print('PASS: all ' + str(len(records)) + ' regenerated compiler sources match upstream')
