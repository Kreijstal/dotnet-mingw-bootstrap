"""Verify exact publish bytes and normal compiler/host/runtime-pack selection."""
from pathlib import Path
import hashlib
import json
import sys
import xml.etree.ElementTree as E

sdk, out = (Path(p).resolve() for p in sys.argv[1:])
pack = sdk / 'packs/Microsoft.NETCore.App.Runtime.win-x64/8.0.31'
files = E.parse(pack / 'data/RuntimeList.xml').getroot()
assert len(files) == 175
for item in files:
    source = pack / item.get('Path')
    actual = out / 'published' / source.name
    assert actual.is_file() and hashlib.sha256(actual.read_bytes()).digest() == hashlib.sha256(source.read_bytes()).digest(), source.name
deps = json.loads((out / 'published/Check.deps.json').read_text())
runtime_name = 'runtimepack.Microsoft.NETCore.App.Runtime.win-x64/8.0.31'
assert runtime_name in deps['libraries'] and deps['libraries'][runtime_name]['type'] == 'runtimepack', deps['libraries']
for obj in [out / 'obj/Release/net8.0', out / 'obj/Release/net8.0/win-x64']:
    compiler = (obj / 'compiler.txt').read_text(encoding='utf-8-sig').replace('\\', '/').strip()
    assert compiler.endswith('|csc49.cmd') and 'compiler-packs/roslyn-4.9.2' in compiler, compiler
    host = (obj / 'apphost-source.txt').read_text(encoding='utf-8-sig').replace('\\', '/').strip()
    assert '/Microsoft.NETCore.App.Host.win-x64/8.0.31/' in host, host
print('PASS default source compiler/apphost selection; ordinary self-contained publish selects8.0.31 and all175 exact runtime-pack bytes')
