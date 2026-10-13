#!/usr/bin/env python3
"""Generate selected sequential paragraph-service records from original Pts.cs."""
from pathlib import Path
import re
import sys
source,output=map(Path,sys.argv[1:]);text=source.read_text()
primitive={'int':'int32_t','uint':'uint32_t','IntPtr':'void*'}
records={name:body for name,body in re.findall(r'\bstruct\s+(\w+)\s*\{(.*?)\n\s{8}\}',text,re.S)}
delegates=set(re.findall(r'\bdelegate\s+\w+\s+(\w+)\s*\(',text))
lines=['// Generated from original WPF Pts.cs.','#pragma once','#include <cstdint>'];done=set()
def emit(name):
    if name in done:return
    if name not in records:raise SystemExit('Missing PTS record: '+name)
    fields=re.findall(r'\binternal\s+(\w+)\s+(\w+)\s*;',records[name])
    if not fields:raise SystemExit('Empty PTS record: '+name)
    for kind,field in fields:
        if kind in records:emit(kind)
    lines.append('struct '+name+' {')
    for kind,field in fields:
        native=primitive.get(kind)
        if native is None:
            if kind in records:native=kind
            elif kind in delegates:native='void*'
            else:raise SystemExit('Unsupported PTS field: '+kind+' '+field)
        lines.append('    '+native+' '+field+';')
    lines.append('};');done.add(name)
for name in ('FSIMETHODS','FSCONTEXTINFO'):emit(name)
for name,value in re.findall(r'\bconst int\s+(tserr\w+)\s*=\s*(-?\d+)\s*;',text):lines.append(f'constexpr int32_t PTS_{name} = {value};')
output.write_text('\n'.join(lines)+'\n')
