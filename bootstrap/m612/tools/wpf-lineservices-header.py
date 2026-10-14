#!/usr/bin/env python3
"""Generate selected native LS records/constants from their original C# ABI."""
from pathlib import Path
import re
import sys

source,output=map(Path,sys.argv[1:])
text=source.read_text()
primitive={'uint':'uint32_t','int':'int32_t','ushort':'uint16_t','char':'uint16_t','IntPtr':'void*'}
records=('EscStringInfo','LsTbd','LsDevRes','LsIMethods','LscbkRedefined','LsContextInfo',
         'LsPap','LsLineProps','LsChp','LsTxM','LsHeights','LSPOINT','LSRECT','LsLInfo','LsLineWidths',
         'LsQSubInfo','LsTextCell','LsULInfo','LsStInfo','ObjDim','InlineInit',
         'LsNeighborInfo','LsCharRunInfo','LsGlyphRunInfo','LsHyph')
lines=['// Generated from original WPF LineServices.cs.','#pragma once','#include <cstdint>']
for name in records:
    match=re.search(r'\bstruct\s+'+name+r'\s*\{(.*?)\n    \}',text,re.S)
    if not match: raise SystemExit('Missing LS record: '+name)
    fields=re.findall(r'public\s+(\w+\*?)\s+(\w+)\s*;',match.group(1))
    if not fields: raise SystemExit('No fields in LS record: '+name)
    lines.append('struct '+name+' {')
    for kind,field in fields:
        if kind.endswith('*'):
            pointee=kind[:-1]
            if pointee in primitive: native=primitive[pointee]+'*'
            elif pointee=='GlyphOffset': native='void*' # external record; pointer ABI only
            else: raise SystemExit('Unsupported LS pointer field: '+kind+' '+field)
            lines.append('    '+native+' '+field+';')
            continue
        if kind not in primitive:
            if kind in records: native=kind
            elif kind=='Plsrun': native='uint32_t'
            elif re.search(r'\benum\s+'+kind+r'\b',text): native='int32_t'
            elif re.search(r'\bdelegate\s+\w+\s+'+kind+r'\s*\(',text): native='void*'
            else: raise SystemExit('Unsupported LS ABI field: '+kind+' '+field)
        else: native=primitive[kind]
        lines.append('    '+native+' '+field+';')
    lines.append('};')
body=re.search(r'\benum\s+LsErr\s*\{(.*?)\n    \}',text,re.S).group(1)
for key,value in re.findall(r'\b(\w+)\s*=\s*(-?\d+)\s*[,\n]',body):
    lines.append(f'constexpr int32_t LS_{key} = {value};')
infinite=re.search(r'const int\s+IdealInfiniteWidth\s*=\s*(0x[0-9A-Fa-f]+|\d+)',text)
if not infinite: raise SystemExit('Missing original IdealInfiniteWidth')
lines.append(f'constexpr int32_t LS_IdealInfiniteWidth = {infinite.group(1)};')
output.write_text('\n'.join(lines)+'\n')
