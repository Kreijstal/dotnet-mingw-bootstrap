#!/usr/bin/env python3
"""illinkxml.py: what eng/illink.targets embeds into a runtime library, without MSBuild.

usage: illinkxml.py combine <out.xml> <in.xml>...
         ILLink.Tasks' CombineLinkerXmlFiles: the children of every input's <linker> root under
         one <linker> (inputs that do not exist are skipped; no output if none exist)
       illinkxml.py resources <out.xml> <template> <AssemblyName> <StringResourcesName>
         GenerateResourcesSubstitutionFile: eng/ILLink.Substitutions.Resources.template filled in
"""
import os, sys
import xml.etree.ElementTree as ET

def combine(out, files):
    root = ET.Element('linker'); n = 0
    for f in files:
        if not os.path.exists(f): continue
        n += 1
        for el in ET.parse(f).getroot(): root.append(el)
    if n == 0: return 0
    ET.indent(root) if hasattr(ET, 'indent') else None
    open(out, 'w', encoding='utf-8', newline='\n').write(ET.tostring(root, encoding='unicode') + '\n')
    return n

if sys.argv[1] == 'combine':
    n = combine(sys.argv[2], sys.argv[3:])
    print('%s: %d file(s)' % (os.path.basename(sys.argv[2]), n))
elif sys.argv[1] == 'resources':
    out, tpl, asm, srn = sys.argv[2:6]
    t = open(tpl, encoding='utf-8').read().replace('{AssemblyName}', asm).replace('{StringResourcesName}', srn)
    open(out, 'w', encoding='utf-8', newline='\n').write(t)
