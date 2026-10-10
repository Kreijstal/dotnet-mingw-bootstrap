#!/usr/bin/env python3
"""s12nupkg.py <content-dir> <nuspec> <out-dir> [--exclude GLOB]...

Zip a package layout into <out-dir>/<id>.<version>.nupkg (lower case, as NuGet names files in a
feed).  The nuspec is stored at the root as <id>.nuspec without its <files> element (the layout
already is the package content).  Entries are sorted and carry a fixed timestamp, so the same
input gives the same bytes.  Prints the package path.
"""
import fnmatch, os, re, sys, zipfile

args = sys.argv[1:]
excl = []
while '--exclude' in args:
    i = args.index('--exclude'); excl.append(args[i + 1]); del args[i:i + 2]
src, nuspec, outdir = args
s = open(nuspec, encoding='utf-8-sig').read()
s = re.sub(r'\s*<files>.*?</files>', '', s, flags=re.S)
pid = re.search(r'<id>\s*([^<\s]+)\s*</id>', s).group(1)
ver = re.search(r'<version>\s*([^<\s]+)\s*</version>', s).group(1)
os.makedirs(outdir, exist_ok=True)
out = os.path.join(outdir, ('%s.%s.nupkg' % (pid, ver)).lower())
skip = {'.signature.p7s', '[Content_Types].xml'}
files = []
for dp, dn, fn in os.walk(src):
    dn[:] = [d for d in dn if d not in ('_rels', 'package', 'obj', 'bin')]
    for f in fn:
        full = os.path.join(dp, f)
        rel = os.path.relpath(full, src).replace('\\', '/')
        if rel in skip or rel.lower().endswith(('.nuspec', '.csproj')):
            continue
        if any(fnmatch.fnmatch(rel, e) for e in excl):
            continue
        files.append((rel, full))
files.sort()
T = (2000, 1, 1, 0, 0, 0)
with zipfile.ZipFile(out + '.tmp', 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr(zipfile.ZipInfo(pid + '.nuspec', T), s.encode('utf-8'), zipfile.ZIP_DEFLATED)
    for rel, full in files:
        zi = zipfile.ZipInfo(rel, T); zi.compress_type = zipfile.ZIP_DEFLATED
        with open(full, 'rb') as fh:
            z.writestr(zi, fh.read())
os.replace(out + '.tmp', out)
print(out)
