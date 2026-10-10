#!/usr/bin/env python3
"""s12sbrpdeps.py <sbrp-src> <tfm-filter> <id/version>...

The closure of source-build-reference-packages reference packages needed to build the given ones:
follows the PackageReference items of each SBRP project for the target frameworks that pass the
filter (';'-separated, as DotNetTargetFrameworkFilter).  Prints "id/version" lines in build order
(dependencies first); ids missing from SBRP go to stderr as "MISSING id/version (needed by ...)".
Text-only and targeting packs are not followed (s12-sbrp.sh builds those separately).
"""
import os, re, sys

root, flt = sys.argv[1], [t.strip().lower() for t in sys.argv[2].replace('%3b', ';').split(';') if t.strip()]
refsrc = os.path.join(root, 'src', 'referencePackages', 'src')
text = os.path.join(root, 'src', 'textOnlyPackages', 'src')
tpacks = os.path.join(root, 'src', 'targetPacks', 'ILsrc')
# SBRP's Directory.Build.targets: netstandard1.x projects of these ids disable the implicit
# NETStandard.Library reference (they are part of its graph); all others get NETStandard.Library 1.6.1
NS1X = set(x.lower() for x in """Microsoft.NETCore.Platforms Microsoft.Win32.Primitives System.AppContext System.Collections
System.Collections.Concurrent System.Console System.Diagnostics.Debug System.Diagnostics.DiagnosticSource System.Diagnostics.Tools
System.Diagnostics.Tracing System.Globalization System.Globalization.Calendars System.IO System.IO.Compression
System.IO.Compression.ZipFile System.IO.FileSystem System.IO.FileSystem.Primitives System.Linq System.Linq.Expressions
System.Net.Http System.Net.Primitives System.Net.Sockets System.ObjectModel System.Reflection System.Reflection.Extensions
System.Reflection.Primitives System.Resources.ResourceManager System.Runtime System.Runtime.Extensions System.Runtime.Handles
System.Runtime.InteropServices System.Runtime.InteropServices.RuntimeInformation System.Runtime.Numerics
System.Security.Cryptography.Algorithms System.Security.Cryptography.Cng System.Security.Cryptography.Encoding
System.Security.Cryptography.Primitives System.Security.Cryptography.OpenSsl System.Security.Cryptography.X509Certificates
System.Text.Encoding System.Text.Encoding.Extensions System.Text.RegularExpressions System.Threading System.Threading.Tasks
System.Threading.Timer System.Xml.ReaderWriter System.Xml.XDocument""".split())

def nuspec_deps(d):
    out = set()
    for f in os.listdir(d):
        if f.endswith('.nuspec'):
            for r in re.finditer(r'<dependency\s+id="([^"]+)"\s+version="([^"]+)"', open(os.path.join(d, f), encoding='utf-8-sig').read()):
                out.add('%s/%s' % (r.group(1).lower(), r.group(2)))
    return out

# The netstandard1.x package graph (NETStandard.Library 1.6.1 and everything it pulls in).  SBRP builds
# it against the previously-source-built packages; without those, an ns1.x project inside this graph
# that is not in NS1X (e.g. System.Reflection.Emit.ILGeneration) would depend on itself through the
# implicit NETStandard.Library reference.  Such projects are built with DisableImplicitFrameworkReferences
# (marked ":noimplicit"); their explicit PackageReferences are what their reference source needs.
def ns1_graph():
    g, todo = set(), list(nuspec_deps(os.path.join(tpacks, 'netstandard.library', '1.6.1')))
    while todo:
        x = todo.pop()
        if x in g:
            continue
        g.add(x)
        d = os.path.join(refsrc, x)
        if os.path.isdir(d):
            todo += list(nuspec_deps(d))
    return g
NS1G = ns1_graph()

def proj(idv):
    d = os.path.join(refsrc, idv)
    if not os.path.isdir(d):
        return None
    for f in os.listdir(d):
        if f.endswith('.csproj'):
            return os.path.join(d, f)
    return None

def deps(p, idv):
    s = open(p, encoding='utf-8-sig').read()
    tfms = re.search(r'<TargetFrameworks?>([^<]*)</TargetFrameworks?>', s).group(1).lower().split(';')
    keep = [t for t in tfms if t in flt]
    out = set()
    asm = re.search(r'<AssemblyName>([^<]+)</AssemblyName>', s)
    noimplicit = False
    if any(t.startswith('netstandard1') for t in keep) and (not asm or asm.group(1).lower() not in NS1X):
        if idv in NS1G and all(t.startswith('netstandard1') for t in keep):
            noimplicit = True
        else:
            out.add(('netstandard.library', '1.6.1'))
    # ItemGroups conditioned on a TFM, and unconditioned ones
    for m in re.finditer(r'<ItemGroup(?:\s+Condition="([^"]*)")?\s*>(.*?)</ItemGroup>', s, re.S):
        cond, body = m.group(1), m.group(2)
        if cond:
            t = re.search(r"'\$\(TargetFramework\)'\s*==\s*'([^']+)'", cond)
            if t and t.group(1).lower() not in keep:
                continue
        for r in re.finditer(r'<PackageReference\s+Include="([^"]+)"\s+Version="([^"]+)"', body):
            out.add((r.group(1).lower(), r.group(2)))
    return keep, out, noimplicit

order, seen, missing = [], set(), {}
def visit(idv, parent):
    if idv in seen:
        return
    seen.add(idv)
    tp = os.path.join(tpacks, idv)
    if os.path.isdir(tp):
        # a targeting/meta package (s12-sbrp.sh targetpacks builds it): follow its nuspec dependencies
        for f in os.listdir(tp):
            if f.endswith('.nuspec'):
                for r in re.finditer(r'<dependency\s+id="([^"]+)"\s+version="([^"]+)"', open(os.path.join(tp, f), encoding='utf-8-sig').read()):
                    visit('%s/%s' % (r.group(1).lower(), r.group(2)), idv)
        return
    p = proj(idv)
    if p is None:
        if not os.path.isdir(os.path.join(text, idv)):
            missing.setdefault(idv, set()).add(parent)
        return
    keep, ds, noimplicit = deps(p, idv)
    if not keep:
        missing.setdefault(idv + ' (no TFM passes the filter)', set()).add(parent)
        return
    for i, v in sorted(ds):
        visit('%s/%s' % (i, v), idv)
    order.append(idv + (':noimplicit' if noimplicit else ''))

for a in sys.argv[3:]:
    visit(a.lower(), 'root')
print('\n'.join(order))
for k in sorted(missing):
    print('MISSING %s (needed by %s)' % (k, ', '.join(sorted(missing[k]))), file=sys.stderr)
