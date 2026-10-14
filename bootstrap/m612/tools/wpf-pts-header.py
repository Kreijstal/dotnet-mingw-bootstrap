#!/usr/bin/env python3
"""Generate selected source-defined paragraph ABI records, including scoped unions."""
from pathlib import Path
import re
import sys

source, output = map(Path, sys.argv[1:])
text = re.sub(r'/\*.*?\*/|//[^\n]*', '', source.read_text(), flags=re.S)
primitive = {'int': 'int32_t', 'uint': 'uint32_t', 'IntPtr': 'void*'}
delegates = set(re.findall(r'\bdelegate\s+\w+\s+(\w+)\s*\(', text))
records = {}
enums = {}

def closing(start):
    depth = 1
    pos = start + 1
    while depth and pos < len(text):
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    if depth:
        raise SystemExit('Unclosed source record')
    return pos - 1

# Select only records immediately inside PTS. Their nested record names (often
# nested_u) are resolved in the enclosing record, never through a global map.
for match in re.finditer(r'\binternal\s+(struct|enum)\s+(\w+)(?:\s*:\s*(\w+))?\s*\{', text):
    kind, name, base = match.groups()
    if name == 'nested_u':
        continue
    end = closing(match.end() - 1)
    body = text[match.end():end]
    if kind == 'enum':
        if base not in (None, 'int', 'uint'):
            raise SystemExit('Unsupported enum base: ' + str(base))
        enums[name] = (base or 'int', body)
    else:
        records[name] = body

lines = ['// Generated from original WPF Pts.cs.', '#pragma once', '#include <cstdint>', '#include <cstddef>']
done = set()
abi = []

def fields(body):
    # Hide nested definitions while retaining the outer declaration of u.
    nested = {}
    masked = body
    pattern = re.compile(r'\[StructLayout\(LayoutKind\.(Explicit|Sequential)\)\]\s*internal\s+struct\s+(\w+)\s*\{')
    while True:
        match = pattern.search(masked)
        if not match:
            break
        start = match.end() - 1
        depth, end = 1, start + 1
        while depth and end < len(masked):
            depth += (masked[end] == '{') - (masked[end] == '}')
            end += 1
        if depth:
            raise SystemExit('Unclosed nested record')
        nested[match[2]] = (match[1], masked[start + 1:end - 1])
        masked = masked[:match.start()] + ' ' * (end - match.start()) + masked[end:]
    result = re.findall(r'(?:\[FieldOffset\((\d+)\)\]\s*)?\b(?:internal|public)\s+(\w+)\s+(\w+)\s*;', masked)
    if not result:
        raise SystemExit('Empty source record: ' + body[:180])
    return result, nested

def emit_enum(name):
    if name in done:
        return
    base, body = enums[name]
    lines.extend(['enum ' + name + ' : ' + primitive[base] + ' {', body.strip(), '};'])
    done.add(name)

def emit_record(name, body, layout='Sequential'):
    members, nested = fields(body)
    types = {}
    for offset, kind, field in members:
        if layout == 'Explicit' and offset != '0':
            raise SystemExit('Only zero-offset unions are supported: ' + name)
        if layout == 'Sequential' and offset:
            raise SystemExit('FieldOffset on a sequential record: ' + name)
        if kind in nested:
            nested_layout, nested_body = nested[kind]
            native = name + '_' + kind
            emit_record(native, nested_body, nested_layout)
        elif kind in records:
            emit(kind)
            native = kind
        elif kind in enums:
            emit_enum(kind)
            native = kind
        elif kind in delegates:
            native = 'void*'
        elif kind in primitive:
            native = primitive[kind]
        else:
            raise SystemExit('Unsupported PTS field: ' + kind + ' ' + field)
        types[field] = native
    lines.append(('union ' if layout == 'Explicit' else 'struct ') + name + ' {')
    for _, _, field in members:
        lines.append('    ' + types[field] + ' ' + field + ';')
    lines.append('};')
    # Map the flattened native name back to the actual managed nested type.
    managed = name.replace('_nested_u', '+nested_u')
    abi.append((name, managed, [field for _, _, field in members]))

def emit(name):
    if name in done:
        return
    if name not in records:
        raise SystemExit('Missing PTS record: ' + name)
    emit_record(name, records[name])
    done.add(name)

for name in ('FSIMETHODS', 'FSCONTEXTINFO', 'FSRECT', 'FSPOINT', 'FSBBOX',
             'FSFMTR', 'FSPAP', 'FSTXTPROPS', 'FSPARADESCRIPTION', 'FSFTNINFO',
             'FSTRACKDETAILS', 'FSTRACKDESCRIPTION', 'FSPAGEDETAILS',
             'FSSECTIONDESCRIPTION', 'FSSECTIONDETAILS', 'FSTEXTDETAILS',
             'FSLINEDESCRIPTIONSINGLE', 'FSLINEDESCRIPTIONCOMPOSITE', 'FSLINEELEMENT', 'FSLINEVARIANT', 'FSSUBTRACKDETAILS', 'FSCOLUMNINFO',
             'FSFLOATERINIT', 'FSFLOATERPROPS', 'FSFLOATERDETAILS', 'FSSUBPAGEDETAILS', 'FSATTACHEDOBJECTDESCRIPTION', 'FSTABLEOBJINIT', 'FSTABLEOBJPROPS',
             'FSFIGUREPROPS', 'FSFIGUREDETAILS', 'FSTABLEROWPROPS', 'FSTABLEOBJDETAILS', 'FSTABLEDETAILS', 'FSTABLEROWDESCRIPTION', 'FSTABLEROWDETAILS'):
    emit(name)
# Derive callback function signatures from the same source declarations.
for name in ('GetPageDimensions', 'GetSectionProperties', 'GetMainTextSegment',
             'GetFirstPara', 'GetNextPara', 'GetParaProperties', 'CreateParaclient',
             'DestroyParaclient', 'GetTextProperties', 'FormatLine', 'DestroyLine',
             'CreateParaBreakingSession', 'DestroyParaBreakingSession', 'FormatLineVariants', 'ReconstructLineVariant',
             'DestroyLineBreakRecord', 'DestroyMcsclient', 'ObjFormatParaFinite',
             'ObjDestroyPara', 'ObjFormatParaBottomless', 'ObjDestroyBreakRecord', 'GetSectionColumnInfo', 'DuplicateMcsclient',
             'GetObjectHandlerInfo', 'ConvertMcsclient', 'GetFloaterProperties',
             'FormatFloaterContentFinite', 'FormatFloaterContentBottomless',
             'DestroyFloaterContent', 'DestroyFloaterContentBreakRecord', 'GetMCSClientAfterFloater',
             'GetNumberAttachedObjectsBeforeTextLine', 'GetAttachedObjectsBeforeTextLine',
             'GetNumberAttachedObjectsInTextLine', 'GetAttachedObjectsInTextLine',
             'GetTableProperties', 'AutofitTable', 'GetMCSClientAfterTable', 'GetFirstHeaderRow', 'GetFirstFooterRow',
             'GetFirstRow', 'GetNextRow', 'GetDistributionKind', 'GetRowProperties', 'GetCells',
             'FormatCellFinite', 'FormatCellBottomless', 'SetCellHeight', 'DestroyCell', 'DestroyCellBreakRecord',
             'ClearUpdateInfoInCell', 'CalcHorizontalBBoxOfRow', 'GetFigureProperties', 'CalcFigurePosition',
             'UpdGetParaChange', 'ObjComparePara', 'CompareFloaterContents', 'CompareCells',
             'TransferDisplayInfo', 'ObjTransferDisplayInfoPara', 'TransferDisplayInfoInFloaterContent', 'TransferDisplayInfoCell',
             'ObjGetColumnBalancingInfo', 'GetFloaterContentColumnBalancingInfo',
             'GetNumberFootnotes', 'ObjGetNumberFootnotes', 'GetFloaterContentNumberFootnotes', 'GetCellNumberFootnotes'):
    match = re.search(r'\bdelegate\s+int\s+' + name + r'\s*\((.*?)\)\s*;', text, re.S)
    if not match:
        raise SystemExit('Missing source callback: ' + name)
    parameters = []
    for parameter in match[1].split(','):
        parts = parameter.split()
        indirect = parts[0] in ('ref', 'out', 'in')
        if indirect:
            parts = parts[1:]
        kind = parts[0]
        pointer = kind.endswith('*')
        if pointer: kind = kind[:-1]
        if kind in records:
            emit(kind)
            native = kind
        elif kind in enums:
            emit_enum(kind)
            native = kind
        elif kind in primitive:
            native = primitive[kind]
        else:
            raise SystemExit('Unsupported callback parameter: ' + parameter)
        parameters.append(native + ('*' if indirect or pointer else ''))
    lines.append('using Pts' + name + ' = int (WINAPI*)(' + ', '.join(parameters) + ');')
for name, value in re.findall(r'\bconst int\s+((?:tserr|fswdir)\w+|fUDirection|fVDirection|fUVertical)\s*=\s*(-?\d+)\s*;', text):
    lines.append(f'constexpr int32_t PTS_{name} = {value};')
lines.extend(['struct WpfParagraphAbiField { const char* type; const char* field; int size; int offset; };',
              'inline constexpr WpfParagraphAbiField WpfParagraphAbiFields[] = {'])
for name, managed, members in abi:
    for field in members:
        lines.append(f'    {{"{managed}", "{field}", sizeof({name}), offsetof({name}, {field})}},')
lines.append('};')
output.write_text('\n'.join(lines) + '\n')
