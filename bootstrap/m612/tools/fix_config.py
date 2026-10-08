#!/usr/bin/env python3
"""fix_config.py <app.config> <out> <versions-file> [prop=value ...]

Rewrite an MSBuild.exe.config for the from-source toolset:
  * every <bindingRedirect> of an assembly we ship is pointed at the version
    we actually built (versions-file: lines "Name Version");
  * redirects for assemblies we do not ship are left alone;
  * extra toolset properties (prop=value) are added to every <toolset>.
"""
import sys, re
src, out, verfile = sys.argv[1:4]
extra = [a.split('=', 1) for a in sys.argv[4:]]
vers = dict(l.split() for l in open(verfile) if l.strip())
text = open(src, encoding='utf-8-sig').read()

def fix(m):
    block = m.group(0)
    n = re.search(r'name="([^"]+)"', block).group(1)
    if n not in vers: return block
    v = vers[n]
    return re.sub(r'<bindingRedirect oldVersion="[^"]*" newVersion="[^"]*"',
                  '<bindingRedirect oldVersion="0.0.0.0-%s" newVersion="%s"' % (v, v), block)
text = re.sub(r'<dependentAssembly>.*?</dependentAssembly>', fix, text, flags=re.S)
add = []
for k, v in extra:
    pat = r'<property name="%s" value="[^"]*" />' % re.escape(k)
    if re.search(pat, text):   # existing toolset property: replace its value
        text = re.sub(pat, lambda m: '<property name="%s" value="%s" />' % (k, v), text)
    else:
        add.append((k, v))
if add:
    props = ''.join('\n        <property name="%s" value="%s" />' % (k, v) for k, v in add)
    text = re.sub(r'(<toolset toolsVersion="[^"]*">)', lambda m: m.group(1) + props, text)
open(out, 'w', encoding='utf-8', newline='\n').write(text)
