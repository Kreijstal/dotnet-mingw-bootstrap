#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Resolve the exports of core/dll/wpfgfx.def against the static libraries.

Most wpfgfx exports are C++ functions without extern "C".  MSVC's link.exe
matches an undecorated .def name to the decorated C++ symbol; GNU ld and lld
do not.  This writes a .def whose entries alias the export name to the
(Itanium-mangled, global-namespace) symbol found in the libraries, plus a
response file of --undefined options so that the archive members holding
them get linked.

usage: gendef.py NM IN.def DLLNAME OUT.def OUT.rsp LIB.a...
"""
import re
import subprocess
import sys


def main(argv):
    nm, in_def, dll_name, out_def, out_rsp = argv[1:6]
    libs = argv[6:]
    defined = {}
    for lib in libs:
        out = subprocess.run([nm, '-g', '--defined-only', lib],
                             capture_output=True, text=True, check=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) < 3 or parts[1] not in ('T', 'D', 'B', 'R'):
                continue
            sym = parts[2]
            m = re.match(r'_Z(\d+)', sym)
            if m:
                n = int(m.group(1))
                base = sym[m.end():m.end() + n]
                if len(base) == n:
                    defined.setdefault(base, set()).add(sym)
            else:
                defined.setdefault(sym, set()).add(sym)

    exports = []
    in_exports = False
    for line in open(in_def, encoding='latin-1'):
        line = line.split(';')[0].strip()
        if not line:
            continue
        if line == 'EXPORTS':
            in_exports = True
            continue
        if in_exports:
            exports.append(line.split()[0])

    missing = []
    with open(out_def, 'w', newline='\n') as d, open(out_rsp, 'w', newline='\n') as r:
        d.write('LIBRARY %s\nEXPORTS\n' % dll_name)
        for name in exports:
            syms = sorted(defined.get(name, ()))
            if len(syms) != 1:
                missing.append('%s (%s)' % (name, ', '.join(syms) or 'not found'))
                continue
            sym = syms[0]
            d.write('    %s\n' % name if sym == name else '    %s=%s\n' % (name, sym))
            r.write('-Wl,--undefined=%s\n' % sym)
    if missing:
        sys.stderr.write('gendef.py: cannot resolve exports:\n  ' + '\n  '.join(missing) + '\n')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
