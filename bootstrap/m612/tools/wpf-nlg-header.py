#!/usr/bin/env python3
"""Derive legacy WPF spelling COM slots and identities from NLGSpellerInterop.cs."""
from pathlib import Path
import re
import sys
import uuid

source, output = map(Path, sys.argv[1:])
text = re.sub(r'//[^\n]*', '', source.read_text())
names = ('ILexicon', 'ITextContext', 'ITextChunk', 'ISentence', 'ITextSegment', 'IProcessingOptions')
types = {'Int32': 'LONG', 'IntPtr': 'const WCHAR*', 'bool': 'VARIANT_BOOL',
         'string': 'BSTR', 'object': 'VARIANT', 'STextRange': 'NlTextRange', 'RangeRole': 'LONG',
         'MS.Win32.UnsafeNativeMethods.IEnumVariant': 'IEnumVARIANT*'}
types.update({name: 'Nl' + name + '*' for name in names})
lines = ['// Generated from original WPF NLGSpellerInterop.cs.', '#pragma once',
         '#include <windows.h>', '#include <oleauto.h>',
         'struct NlTextRange { LONG start; LONG length; };']
lines += ['struct Nl' + name + ';' for name in names]
roles = re.search(r'enum RangeRole\s*\{([^}]+)\}', text)
if not roles:
    raise SystemExit('Missing original NLG range roles')
for name, value in re.findall(r'(\w+)\s*=\s*(\d+)', roles[1]):
    lines.append('inline constexpr LONG NL_' + name + ' = ' + value + ';')


def guid(name, value):
    fields = uuid.UUID(value).fields
    tail = uuid.UUID(value).bytes[8:]
    return f'inline constexpr GUID {name} = {{0x{fields[0]:08x},0x{fields[1]:04x},0x{fields[2]:04x},{{' + ','.join(f'0x{b:02x}' for b in tail) + '}};'


for name in names:
    match = re.search(r'\[Guid\("([^"\n]+)"\)\]\s*(?:private|internal) interface ' + name + r'\s*\{([^}]+)\}', text)
    if not match:
        raise SystemExit('Missing original NLG interface: ' + name)
    lines.append(guid('NL_IID_' + name, match[1]))
    lines.append('struct Nl' + name + ' : IUnknown {')
    methods = re.findall(r'void\s+(\w+)\s*\(([^;]*)\)\s*;', match[2])
    if not methods:
        raise SystemExit('No original NLG slots: ' + name)
    for method, arguments in methods:
        arguments = re.sub(r'\[[^]]+\]', '', arguments).strip()
        native = []
        for argument in arguments.split(',') if arguments else []:
            parts = argument.split()
            out = parts[0] == 'out'
            if out:
                parts = parts[1:]
            kind, parameter = parts
            native.append(types[kind] + ('*' if out else '') + ' ' + parameter)
        suffix = ' { return E_NOTIMPL; }' if method.startswith('stub_') else ' = 0;'
        lines.append('    virtual HRESULT STDMETHODCALLTYPE ' + method + '(' + ', '.join(native) + ')' + suffix)
    lines.append('};')

for name in ('CLSID_ITextContext', 'CLSID_ITextChunk', 'CLSID_Lexicon'):
    match = re.search(r'\b' + name + r'\s*=\s*new Guid\(([^)]+)\)', text)
    if not match:
        raise SystemExit('Missing original NLG class: ' + name)
    value = match[1].strip()
    if value.startswith('"'):
        lines.append(guid('NL_' + name, value.strip('"')))
    else:
        parts = [p.strip() for p in value.split(',')]
        if len(parts) != 11:
            raise SystemExit('Unexpected original GUID: ' + name)
        lines.append('inline constexpr GUID NL_' + name + ' = {' + ','.join(parts[:3]) + ',{' + ','.join(parts[3:]) + '}};')
output.write_text('\n'.join(lines) + '\n')
