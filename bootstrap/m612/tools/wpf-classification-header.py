#!/usr/bin/env python3
"""Keep the native script/flag values tied to original WPF source enums."""
from pathlib import Path
import re
import sys

source,output=map(Path,sys.argv[1:])
text=source.read_text()
lines=['// Generated from original WPF UnicodeClasses.cs.','#pragma once']
for name in ('ScriptID','ItemClass','DirectionClass','CharacterAttributeFlags','CharBreakingType'):
    body=re.search(r'\benum\s+'+name+r'\s*:[^{]+\{(.*?)\}',text,re.S).group(1)
    for key,value in re.findall(r'\b(\w+)\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+)',body):
        lines.append(f'constexpr unsigned WPF_{name}_{key} = {value};')
output.write_text('\n'.join(lines)+'\n')
