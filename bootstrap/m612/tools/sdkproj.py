#!/usr/bin/env python3
"""sdkproj.py: evaluate one SDK-style C# project (Microsoft.NET.Sdk) for one
target framework, the way the SDK would feed csc, and write what a direct csc
command line needs (see sdkproj.sh, which consumes it).

usage: sdkproj.py <project> <outdir> [-p Name=Value]... [--pre file]... [--post file]...
                  [--no-default-items]

Writes into <outdir>:
  items       Compile items (absolute paths)
  defines     DefineConstants
  resources   EmbeddedResource: path<TAB>manifest-name<TAB>generate-source(0|1)<TAB>class-full-name
  usings      global usings (ImplicitUsings set + <Using> items): one C# line each
  props       key=value of the properties the build needs
  prefs       ProjectReference: absolute path
  pkgrefs     PackageReference names
  ivt         InternalsVisibleTo items (name)

The SDK conventions implemented: default Compile/EmbeddedResource globs
(EnableDefault*Items), Compile/EmbeddedResource Remove/Update against them,
manifest resource names (CreateCSharpManifestResourceName: LogicalName,
ManifestResourceName, the DependentUpon convention for a .resx with a .cs of
the same name, else RootNamespace + directory names made valid identifiers +
file name), culture-specific .resx files left out (satellite assemblies are
not built), Arcade's GenerateResxSource (GenerateSource/Namespace/ClassName).
"""
import os, re, sys, glob, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import csproj_items as ci

CULTURE_RE = re.compile(r'\.[a-z]{2,3}(-[A-Za-z]{2,4})*(-[A-Za-z]{2})?\.resx$')

def everett(seg):
    """MSBuild MakeValidEverettIdentifier for one folder name"""
    s = re.sub(r'[^\w]', '_', seg)
    if s and s[0].isdigit(): s = '_' + s
    return s

def cs_type_of(path):
    """namespace + first class of a .cs file (DependentUpon convention)"""
    try: t = open(path, encoding='utf-8-sig', errors='replace').read()
    except OSError: return None
    t = re.sub(r'//[^\n]*|/\*.*?\*/', '', t, flags=re.S)
    ns = re.search(r'\bnamespace\s+([\w.]+)', t)
    cl = re.search(r'\b(?:class|struct|record)\s+(\w+)', t)
    if not cl: return None
    return (ns.group(1) + '.' if ns else '') + cl.group(1)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('project'); ap.add_argument('outdir')
    ap.add_argument('-p', action='append', default=[])
    ap.add_argument('--pre', action='append', default=[])
    ap.add_argument('--post', action='append', default=[])
    ap.add_argument('--no-default-items', action='store_true')
    a = ap.parse_args()
    proj = os.path.abspath(a.project); pd = os.path.dirname(proj)
    props = {}
    for kv in a.p:
        k, _, v = kv.partition('='); props[k] = v
    if 'TargetFramework' in props:
        ident, ver = ci.tfm_info(props['TargetFramework'])
        props.setdefault('TargetFrameworkIdentifier', ident)
        props.setdefault('TargetFrameworkVersion', 'v' + '.'.join(str(x) for x in (ver + (0,))[:max(2, len(ver))]))
    name = os.path.splitext(os.path.basename(proj))[0]
    base = dict(MSBuildProjectDirectory=pd, MSBuildProjectName=name, MSBuildProjectFile=os.path.basename(proj),
                MSBuildProjectExtension='.csproj', MSBuildProjectFullPath=proj, Language='C#', DefaultLanguageSourceExtension='.cs')
    kinds = {'Compile', 'EmbeddedResource', 'None', 'Content'}

    def evaluate(seed):
        ev = ci.Evaluator(props, list(props))
        for k, v in base.items(): ev.props.setdefault(k, v)
        for k, v in seed.items(): ev.items[k] = [(f, {}) for f in v]
        for f in a.pre: ev.load(f, kinds, True)
        ev.props['MSBuildProjectDirectory'] = pd
        ev.load(proj, kinds, True)
        ev.props['MSBuildProjectDirectory'] = pd
        for f in a.post: ev.load(f, kinds, True)
        return ev

    ev = evaluate({})
    pr = ev.props
    tru = lambda k, d='': (pr.get(k, d) or d).strip().lower() == 'true'
    if not a.no_default_items and pr.get('EnableDefaultItems', 'true').strip().lower() != 'false':
        def walk(pat):
            out = []
            for f in sorted(glob.glob(os.path.join(pd, '**', pat), recursive=True)):
                r = os.path.relpath(f, pd).replace('\\', '/')
                if r.split('/')[0] in ('bin', 'obj'): continue
                out.append(os.path.normpath(f))
            return out
        seed = {}
        if pr.get('EnableDefaultCompileItems', 'true').strip().lower() != 'false': seed['Compile'] = walk('*.cs')
        if pr.get('EnableDefaultEmbeddedResourceItems', 'true').strip().lower() != 'false': seed['EmbeddedResource'] = walk('*.resx')
    else:
        seed = {}
    implicit = ('System', 'System.Collections.Generic', 'System.IO', 'System.Linq', 'System.Net.Http', 'System.Threading', 'System.Threading.Tasks')
    if (pr.get('ImplicitUsings', '') or '').strip().lower() in ('enable', 'true'):
        seed['Using'] = list(implicit)    # before the project, so <Using Remove> applies
    if seed:
        ev = evaluate(seed); pr = ev.props
    os.makedirs(a.outdir, exist_ok=True)
    W = lambda n, lines: open(os.path.join(a.outdir, n), 'w', newline='\n').write(''.join(l + '\n' for l in lines))

    seen, items = set(), []
    for p, _ in ev.items.get('Compile', []):
        p = os.path.normpath(p if os.path.isabs(p) else os.path.join(pd, p))
        if p in seen or not p.endswith('.cs'): continue
        seen.add(p); items.append(p.replace('\\', '/'))
    W('items', items)
    W('defines', [pr.get('DefineConstants', '')])

    rootns = pr.get('RootNamespace') or pr.get('AssemblyName') or name
    res, rseen = [], set()
    for p, m in ev.items.get('EmbeddedResource', []):
        p = os.path.normpath(p if os.path.isabs(p) else os.path.join(pd, p))
        if p in rseen: continue
        rseen.add(p)
        if p.endswith('.resx') and CULTURE_RE.search(os.path.basename(p)) and not m.get('LogicalName'): continue
        fn = os.path.basename(p)
        rel = os.path.relpath(p, pd).replace('\\', '/')
        link = m.get('Link') or (m.get('LinkBase', '').rstrip('/\\') + '/' + fn if m.get('LinkBase') else '')
        if rel.startswith('../') or os.path.isabs(rel):
            rel = (link or fn).replace('\\', '/')
        dirs = [everett(d) for d in rel.split('/')[:-1] if d]
        gen = (m.get('GenerateSource', '') or (pr.get('GenerateResxSource', '') if p.endswith('.resx') else '') or '').lower() == 'true'
        if p.endswith('.resx'):
            stem = os.path.splitext(fn)[0]
            if m.get('LogicalName'): manifest = m['LogicalName']
            elif m.get('ManifestResourceName'): manifest = m['ManifestResourceName'] + '.resources'
            else:
                dep = m.get('DependentUpon') or (stem + '.cs' if os.path.exists(os.path.join(os.path.dirname(p), stem + '.cs')) else '')
                t = cs_type_of(os.path.join(os.path.dirname(p), dep)) if dep and not gen else None
                manifest = (t if t else '.'.join([rootns] + dirs + [stem])) + '.resources'
            cls = ''
            if gen:
                cname = m.get('ClassName') or ''
                if '.' in cname: cls = cname
                elif m.get('Namespace'): cls = m['Namespace'] + '.' + (cname or stem)
                else: cls = manifest[:-len('.resources')] if not cname else '.'.join([rootns] + dirs + [cname])
                if not m.get('LogicalName') and not m.get('ManifestResourceName'): manifest = cls + '.resources'
                # a compiled, checked-in designer class of the same name (System.CommandLine) wins
                des = os.path.normpath(os.path.splitext(p)[0] + '.Designer.cs')
                if des in seen and cs_type_of(des) == cls:
                    gen, manifest = False, (m.get('LogicalName') or '.'.join([rootns] + dirs + [stem]) + '.resources')
            res.append('\t'.join([p.replace('\\', '/'), manifest, '1' if gen else '0', cls]))
        else:
            manifest = m.get('LogicalName') or m.get('ManifestResourceName') or '.'.join([rootns] + dirs + [fn])
            res.append('\t'.join([p.replace('\\', '/'), manifest, '0', '']))
    W('resources', res)

    ulines = []
    for u, m in ev.items.get('Using', []):
        if m.get('Alias'): ulines.append('global using %s = global::%s;' % (m['Alias'], u))
        elif (m.get('Static', '') or '').lower() == 'true': ulines.append('global using static global::%s;' % u)
        else: ulines.append('global using global::%s;' % u)
    # (<Using Remove=...> was applied by the evaluator) de-duplicate
    W('usings', list(dict.fromkeys(ulines)))

    keep = ['AssemblyName', 'RootNamespace', 'Nullable', 'LangVersion', 'AllowUnsafeBlocks', 'OutputType', 'NoWarn',
            'StrongNameKeyId', 'SignAssembly', 'PublicSign', 'AssemblyVersion', 'FileVersion', 'Version', 'VersionPrefix',
            'InformationalVersion', 'StartupObject', 'ImplicitUsings', 'TargetFramework', 'Features', 'CheckForOverflowUnderflow',
            'GenerateAssemblyInfo', 'Deterministic', 'IsPackable', 'PackageId', 'EnableDefaultCompileItems', 'DefineConstants']
    W('props', ['%s=%s' % (k, pr.get(k, '')) for k in keep])
    pref = []
    for r, _ in ev.items.get('ProjectReference', []):
        r = r.replace('\\', '/')
        pref.append(os.path.normpath(r if os.path.isabs(r) else os.path.join(pd, r)).replace('\\', '/'))
    W('prefs', pref)
    W('self', [proj.replace('\\', '/')])
    W('pkgrefs', [r for r, _ in ev.items.get('PackageReference', [])])
    W('ivt', [r for r, _ in ev.items.get('InternalsVisibleTo', [])])
    W('warnings', ev.warnings)

if __name__ == '__main__':
    main()
