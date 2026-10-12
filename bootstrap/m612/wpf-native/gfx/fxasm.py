#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Minimal Direct3D 9 shader assembler for WPF's text pixel shaders.

dotnet/wpf keeps the D3D9 text rendering shaders as hand-written shader
assembly inside D3DX effect files (core/hw/shaders/hlslTextShaders*.fx) and
ships the assembled tokens in a generated core/hw/Shaders.rc (made with the
D3DX-based ShaderGen tool).  This script assembles the asm blocks of those
.fx files itself, so the shader tokens have a source lineage, and writes the
same resource script layout.

Only the subset used by these shaders is implemented: ps_1_1 and ps_2_0,
arithmetic instructions, tex/texld, dcl, co-issue ('+'), _sat/_x2/_x4/_d2
result modifiers, '-' and '1-' source modifiers, write masks and swizzles.

usage: fxasm.py OUT.rc NAME=FILE.fx [NAME=FILE.fx ...]
       fxasm.py --check REFERENCE.rc NAME=FILE.fx ...
"""
import re
import sys

OPCODES = {
    'nop': 0, 'mov': 1, 'add': 2, 'sub': 3, 'mad': 4, 'mul': 5, 'rcp': 6,
    'rsq': 7, 'dp3': 8, 'dp4': 9, 'min': 10, 'max': 11, 'slt': 12, 'sge': 13,
    'exp': 14, 'log': 15, 'lrp': 18, 'frc': 19, 'abs': 35, 'nrm': 36,
    'pow': 32, 'cnd': 80, 'cmp': 88, 'tex': 66, 'texld': 66, 'texkill': 65,
    'dcl': 31,
}
NSRC = {'nop': 0, 'mov': 1, 'rcp': 1, 'rsq': 1, 'exp': 1, 'log': 1,
        'frc': 1, 'abs': 1, 'nrm': 1, 'add': 2, 'sub': 2, 'mul': 2, 'dp3': 2,
        'dp4': 2, 'min': 2, 'max': 2, 'slt': 2, 'sge': 2, 'pow': 2,
        'mad': 3, 'lrp': 3, 'cnd': 3, 'cmp': 3, 'tex': 0, 'texld': 2,
        'texkill': 0}
# D3DSHADER_PARAM_REGISTER_TYPE
REGTYPES = {'r': 0, 'v': 1, 'c': 2, 't': 3, 'oC': 8, 'oDepth': 9, 's': 10}
COMP = {'x': 0, 'y': 1, 'z': 2, 'w': 3, 'r': 0, 'g': 1, 'b': 2, 'a': 3}
SHIFT = {'': 0, 'x2': 1, 'x4': 2, 'x8': 3, 'd2': 15, 'd4': 14, 'd8': 13}
SAMPLER_TYPE = {'2d': 2, 'cube': 3, 'volume': 4}


class AsmError(Exception):
    pass


def reg_bits(name):
    m = re.fullmatch(r'(oC|oDepth|[rvcts])(\d*)', name)
    if not m:
        raise AsmError('bad register %r' % name)
    rtype = REGTYPES[m.group(1)]
    num = int(m.group(2) or 0)
    return (num & 0x7ff) | ((rtype & 7) << 28) | (((rtype >> 3) & 3) << 11)


def dst_token(text, mods):
    reg, _, mask = text.partition('.')
    tok = 0x80000000 | reg_bits(reg)
    if mask:
        m = 0
        for ch in mask:
            m |= 1 << COMP[ch]
    else:
        m = 0xf
    tok |= m << 16
    shift = ''
    for mod in mods:
        if mod == 'sat':
            tok |= 1 << 20
        elif mod == 'pp':
            tok |= 2 << 20
        elif mod == 'centroid':
            tok |= 4 << 20
        elif mod in SHIFT:
            shift = mod
        else:
            raise AsmError('bad instruction modifier %r' % mod)
    tok |= SHIFT[shift] << 24
    return tok


def src_token(text):
    srcmod = 0
    if text.startswith('1-'):
        srcmod, text = 6, text[2:]
    elif text.startswith('-'):
        srcmod, text = 1, text[1:]
    reg, _, swz = text.partition('.')
    if reg.endswith('_bx2'):
        reg = reg[:-4]
        srcmod = 5 if srcmod == 1 else 4
    tok = 0x80000000 | reg_bits(reg) | (srcmod << 24)
    if swz:
        comps = [COMP[ch] for ch in swz]
        comps += [comps[-1]] * (4 - len(comps))
    else:
        comps = [0, 1, 2, 3]
    for i, c in enumerate(comps):
        tok |= c << (16 + 2 * i)
    return tok


def assemble(lines):
    """Assemble one asm block; returns the list of DWORD tokens."""
    tokens = []
    major = None
    for raw in lines:
        line = raw.split('//')[0].split(';')[0].strip()
        if not line:
            continue
        m = re.fullmatch(r'ps[._](\d)[._](\d)', line)
        if m:
            major, minor = int(m.group(1)), int(m.group(2))
            tokens.append(0xffff0000 | (major << 8) | minor)
            continue
        if major is None:
            raise AsmError('instruction before version: %r' % line)
        coissue = line.startswith('+')
        if coissue:
            line = line[1:].strip()
        op, _, rest = line.partition(' ')
        args = [a.strip() for a in rest.split(',')] if rest.strip() else []
        opname, *mods = op.split('_')
        # dcl_2d / dcl_cube: sampler declarations
        if opname == 'dcl':
            if major < 2:
                raise AsmError('dcl in ps_1_x not supported')
            if mods:
                dcl = 0x80000000 | (SAMPLER_TYPE[mods[0]] << 27)
            else:
                dcl = 0x80000000
            tokens += [0x02000000 | OPCODES['dcl'], dcl, dst_token(args[0], [])]
            continue
        if opname not in OPCODES:
            raise AsmError('unknown instruction %r' % op)
        dst, srcs = args[0], args[1:]
        if len(srcs) != NSRC[opname]:
            raise AsmError('wrong operand count in %r' % line)
        if opname == 'sub' and major >= 2:
            # ps_2_0 has no sub; like the D3DX assembler, emit add with a
            # negated second source
            opname = 'add'
            s = srcs[1]
            srcs = [srcs[0], s[1:] if s.startswith('-') else '-' + s]
        body = [dst_token(dst, mods)] + [src_token(s) for s in srcs]
        instr = OPCODES[opname]
        if major >= 2:
            instr |= len(body) << 24
        if coissue:
            instr |= 1 << 30
        tokens.append(instr)
        tokens += body
    tokens.append(0x0000ffff)
    return tokens


def parse_fx(path):
    """Yield (technique, pass, asm lines) for every 'PixelShader = asm {}'."""
    text = open(path, encoding='latin-1').read()
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    for t in re.finditer(r'technique\s+(\w+)\s*\{', text):
        tech = t.group(1)
        pos = t.end()
        depth = 1
        i = pos
        while depth:
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
            i += 1
        body = text[pos:i - 1]
        for p in re.finditer(r'pass\s+(\w+)\s*\{(.*?)\}\s*;?\s*\}', body, re.S):
            a = re.search(r'PixelShader\s*=\s*asm\s*\{(.*)', p.group(2), re.S)
            if a:
                yield tech, p.group(1), a.group(1).splitlines()


def build(specs):
    out = []
    for spec in specs:
        name, _, path = spec.partition('=')
        for tech, pss, lines in parse_fx(path):
            out.append(('g_PixelShader_%s_%s_%s' % (name, tech, pss),
                        assemble(lines)))
    return out


def write_rc(shaders, f):
    f.write('// Generated by fxasm.py from core/hw/shaders/*.fx; do not edit.\n\n')
    for name, toks in shaders:
        f.write('%s RCDATA\n{\n' % name)
        rows = ['0x%08xL' % t for t in toks]
        for i in range(0, len(rows), 6):
            f.write('    ' + ', '.join(rows[i:i + 6]) +
                    (',' if i + 6 < len(rows) else '') + '\n')
        f.write('}\n\n')  # windres rejects the ';' that rc.exe tolerates


def read_rc(path):
    text = open(path, encoding='latin-1').read()
    res = {}
    for m in re.finditer(r'(\w+)\s+RCDATA\s*\{(.*?)\}', text, re.S):
        res[m.group(1)] = [int(x, 16) for x in
                           re.findall(r'0x([0-9a-fA-F]+)L?', m.group(2))]
    return res


def main(argv):
    if argv[1] == '--check':
        ref = read_rc(argv[2])
        shaders = build(argv[3:])
        bad = 0
        for name, toks in shaders:
            if ref.get(name) == toks:
                print('match     %s (%d tokens)' % (name, len(toks)))
            else:
                bad += 1
                print('MISMATCH  %s' % name)
        missing = set(ref) - {n for n, _ in shaders}
        for n in sorted(missing):
            bad += 1
            print('MISSING   %s' % n)
        print('%d shaders assembled, %d differ from %s' % (len(shaders), bad, argv[2]))
        return 1 if bad else 0
    shaders = build(argv[2:])
    with open(argv[1], 'w', newline='\n') as f:
        write_rc(shaders, f)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
