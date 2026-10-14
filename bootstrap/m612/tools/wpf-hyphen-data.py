#!/usr/bin/env python3
"""Embed checksum-pinned text dictionaries; published apps need no external data."""
from pathlib import Path
import hashlib,json,sys
root,output=map(Path,sys.argv[1:])
manifest=json.loads((root/'manifest.json').read_text())
for name,expected in manifest['files'].items():
    assert hashlib.sha256((root/name).read_bytes()).hexdigest()==expected,name
lines=['// Generated from checksum-pinned text hyphenation patterns.','#pragma once','struct HyphenPatterns { const char* locale; const char* text; size_t size; };']
entries=[]
for index,file in enumerate(sorted((root/'patterns').glob('*/*.dic'))):
 raw=file.read_bytes();name=str(file.relative_to(root));assert hashlib.sha256(raw).hexdigest()==manifest['files'][name],name
 text=raw.decode('utf-8');assert text.splitlines()[0].strip()=='UTF-8',name
 label=f'hyph{index}';assert ')HYPPATTERN"' not in text
 lines.append(f'static const char {label}[] = R"HYPPATTERN('+text+')HYPPATTERN";')
 entries.append(f'    {{"{file.parent.name}", {label}, sizeof({label})-1}},')
lines+=['static const HyphenPatterns builtin_hyphen_patterns[] = {']+entries+['};']
output.write_text('\n'.join(lines)+'\n')
print(f'Embedded {len(entries)} source dictionaries, with verified SHA-256')
