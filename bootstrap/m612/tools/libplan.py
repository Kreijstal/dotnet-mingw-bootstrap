#!/usr/bin/env python3
"""libplan.py: evaluate the src/ projects of the Microsoft.NETCore.App
libraries (dotnet/runtime src/libraries/NetCoreAppLibrary.props) for one
target OS, and write what a direct csc build needs.

usage: libplan.py <runtime-src-root> <tfm e.g. net8.0> <outdir>

For every shared-framework assembly (System.Private.CoreLib excluded, it is
built by corelib.sh) this picks the project and the target framework the
upstream build would use for TargetOS=windows: src/<Name>.csproj with
"$(NetCoreAppCurrent)-windows" if it has one, else "$(NetCoreAppCurrent)";
assemblies without a src project are the checked-in shims
(src/libraries/shims/<Name>/src).

Output, in <outdir>:
  plan.tsv            name  kind(src|shim)  tfm  deps(impl names, ';')   in build order
  p/<name>/items      Compile items (absolute paths)
  p/<name>/defines    DefineConstants (';')
  p/<name>/refs       'ref <Name>'  (Reference -> reference pack) or 'impl <Name>' (ProjectReference)
  p/<name>/analyzers  generator project names (Roslyn components to pass with -analyzer)
  p/<name>/resources  EmbeddedResource items: path<TAB>LogicalName/ManifestResourceName<TAB>kind
  p/<name>/props      key=value of the properties the build step needs
  p/<name>/omit       GenFacadesOmitType items
  skipped.tsv         name  reason
"""
import os, re, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import csproj_items as ci

R, tfm, out = sys.argv[1:4]
R = os.path.abspath(R).replace('\\', '/')
L = R + '/src/libraries'
major = tfm[3:].split('.')[0]
corelib_proj = R + '/src/mono/System.Private.CoreLib/System.Private.CoreLib.csproj'

t = open(L + '/NetCoreAppLibrary.props', encoding='utf-8-sig').read()
netfx = re.search(r'<NetFxReference>(.*?)</NetFxReference>', t, re.S).group(1)
names = [x.strip() for x in re.search(r'<NetCoreAppLibrary>(.*?)</NetCoreAppLibrary>', t, re.S).group(1)
         .replace('$(NetFxReference)', netfx).split(';') if x.strip()]
netcoreapp = ';'.join(names) + ';'
gens = [x.strip() for x in re.search(r'<NetCoreAppLibraryGenerator>(.*?)</NetCoreAppLibraryGenerator>', t, re.S).group(1).split(';') if x.strip()]

def base_props(n, tfm_, tpi):
    p = {
        'TargetFramework': tfm_, 'NetCoreAppCurrent': tfm, 'NetCoreAppPrevious': 'net%d.0' % (int(major) - 1),
        'NetCoreAppMinimum': 'net6.0', 'NetFrameworkMinimum': 'net462', 'NetFrameworkCurrent': 'net481',
        'NetCoreAppCurrentVersion': major + '.0', 'NETCoreAppMaximumVersion': major + '.0',
        'TargetFrameworkIdentifier': '.NETCoreApp', 'TargetFrameworkVersion': 'v%s.0' % major,
        'TargetPlatformIdentifier': tpi, 'TargetOS': 'windows', 'TargetsWindows': 'true', 'TargetArchitecture': 'x64',
        'Configuration': 'Release', 'RuntimeFlavor': 'Mono', 'IsSourceProject': 'true',
        'LibrariesProjectRoot': L + '/', 'CommonPath': L + '/Common/src/', 'CommonPathRoot': L + '/Common/',
        'CommonTestPath': L + '/Common/tests/', 'CoreLibSharedDir': L + '/System.Private.CoreLib/src/',
        'CoreLibProject': corelib_proj, 'UriProject': L + '/System.Private.Uri/src/System.Private.Uri.csproj',
        'RepositoryEngineeringDir': R + '/eng/', 'RepoRoot': R + '/',
        'MSBuildProjectName': n, 'MSBuildProjectExtension': '.csproj', 'Language': 'C#',
        'NetCoreAppLibrary': netcoreapp, 'DefaultLanguageSourceExtension': '.cs',
    }
    return p

def evaluate(n, proj, tfm_, tpi):
    p = base_props(n, tfm_, tpi)
    ev = ci.Evaluator(p, list(p.keys()))
    ev.props.setdefault('MSBuildProjectDirectory', os.path.dirname(proj))
    ev.props['AssemblyName'] = n
    kinds = {'Compile', 'EmbeddedResource', 'None', 'Content'}
    ev.props['StrongNameKeyId'] = 'MicrosoftAspNetCore' if n.startswith('Microsoft.Extensions.') else 'Open'
    # the library's own Directory.Build.props (StrongNameKeyId, ...), without its imports
    dbp = os.path.join(os.path.dirname(os.path.dirname(proj)), 'Directory.Build.props')
    if os.path.exists(dbp): ev.load(dbp, kinds, False)
    ev.load(proj, kinds, True)
    return ev

def pick_tfm(n, proj):
    p = base_props(n, tfm, '')
    ev = ci.Evaluator(p, [k for k in p if k != 'TargetFramework'])
    ev.load(proj, set(), False)
    tfms = ev.props.get('TargetFrameworks') or ev.props.get('TargetFramework') or ''
    tfms = [x.strip() for x in tfms.split(';') if x.strip()]
    for cand in (tfm + '-windows', tfm):
        if cand in tfms: return cand, tfms
    return None, tfms

os.makedirs(out + '/p', exist_ok=True)
plan, skipped, deps = {}, [], {}
for n in names:
    if n == 'System.Private.CoreLib': continue
    srcp = '%s/%s/src/%s.csproj' % (L, n, n)
    shimp = '%s/shims/%s/src/%s.csproj' % (L, n, n)
    if os.path.exists(srcp):
        tf, all_ = pick_tfm(n, srcp)
        if tf is None:
            skipped.append((n, 'no %s or %s-windows target framework (has %s)' % (tfm, tfm, ';'.join(all_)))); continue
        tpi = tf.split('-')[1] if '-' in tf else ''
        ev = evaluate(n, srcp, tf, tpi)
        kind, proj = 'src', srcp
    elif os.path.exists(shimp):
        tf, tpi = tfm, ''
        ev = evaluate(n, shimp, tf, tpi)
        kind, proj = 'shim', shimp
    else:
        skipped.append((n, 'no src or shim project')); continue
    d = out + '/p/' + n
    os.makedirs(d, exist_ok=True)
    pr = ev.props
    # SDK default items (EnableDefaultItems is false for dotnet/runtime, the shims and a few projects turn it on)
    comp = [os.path.normpath(x).replace('\\', '/') for x, _ in ev.items.get('Compile', [])]
    if kind == 'shim' or pr.get('EnableDefaultItems', '').lower() == 'true' or pr.get('EnableDefaultCompileItems', '').lower() == 'true':
        import glob
        pd = os.path.dirname(proj)
        rem = set(os.path.normpath(x) for x, _ in ev.items.get('Compile', []))
        for f in sorted(glob.glob(pd + '/**/*.cs', recursive=True)):
            r = os.path.relpath(f, pd).replace('\\', '/')
            if r.split('/')[0] in ('bin', 'obj'): continue
            f = os.path.normpath(f).replace('\\', '/')
            if f not in comp: comp.append(f)
    seen, items = set(), []
    for c in comp:
        if c in seen: continue
        seen.add(c); items.append(c)
    open(d + '/items', 'w', newline='\n').write(''.join(c + '\n' for c in items))
    open(d + '/defines', 'w', newline='\n').write(pr.get('DefineConstants', '') + '\n')
    refs, an, dl = [], [], []
    for r, m in ev.items.get('Reference', []):
        refs.append('ref ' + os.path.splitext(os.path.basename(r))[0] if r.endswith('.dll') else 'ref ' + r)
    for r, m in ev.items.get('ProjectReference', []):
        r = r.replace('\\', '/')
        rn = os.path.splitext(os.path.basename(r))[0]
        if m.get('OutputItemType') == 'Analyzer':
            an.append(rn); continue
        if m.get('ReferenceOutputAssembly', '').lower() == 'false': continue   # e.g. generators shipped in the package only
        if os.path.normpath(r) == os.path.normpath(corelib_proj): rn = 'System.Private.CoreLib'
        refs.append('impl ' + rn)
        if rn != 'System.Private.CoreLib': dl.append(rn)
    open(d + '/refs', 'w', newline='\n').write(''.join(x + '\n' for x in refs))
    open(d + '/analyzers', 'w', newline='\n').write(''.join(x + '\n' for x in an))
    res = []
    for r, m in ev.items.get('EmbeddedResource', []):
        res.append('\t'.join([os.path.normpath(r).replace('\\', '/'), m.get('LogicalName', '') or m.get('ManifestResourceName', ''),
                              m.get('GenerateSource', ''), m.get('ClassName', '')]))
    open(d + '/resources', 'w', newline='\n').write(''.join(x + '\n' for x in res))
    keep = ['AssemblyName', 'RootNamespace', 'StrongNameKeyId', 'Nullable', 'IsPartialFacadeAssembly', 'CLSCompliant',
            'GeneratePlatformNotSupportedAssembly', 'GeneratePlatformNotSupportedAssemblyMessage', 'StringResourcesPath',
            'StringResourcesName', 'StringResourcesClassName', 'StringResourcesNamespace', 'OmitResources', 'SkipLocalsInit',
            'IncludeDllSafeSearchPathAttribute', 'AllowUnsafeBlocks', 'NoWarn', 'LangVersion', 'Features', 'ContractTypesPartiallyMoved',
            'EnableLibraryImportGenerator', 'EnableComBinder', 'DisableImplicitFrameworkReferences', 'AssemblyVersion',
            'SupportedOSPlatforms', 'UnsupportedOSPlatforms', 'IsTrimmable', 'EnableAOTAnalyzer', 'DefineConstants']
    props = {k: pr.get(k, '') for k in keep}
    props['kind'] = kind; props['tfm'] = tf; props['project'] = proj
    open(d + '/props', 'w', newline='\n').write(''.join('%s=%s\n' % kv for kv in props.items()))
    aa = []
    for a, m in ev.items.get('AssemblyAttribute', []):
        ps = [m[k] for k in sorted(m) if re.match(r'^_Parameter\d+$', k)]
        aa.append('[assembly: %s(%s)]' % (a, ', '.join('"%s"' % x.replace('"', '\\"') for x in ps)))
    open(d + '/attributes', 'w', newline='\n').write(''.join(x + '\n' for x in aa))
    open(d + '/omit', 'w', newline='\n').write(''.join(x + '\n' for x, _ in ev.items.get('GenFacadesOmitType', [])))
    open(d + '/warnings', 'w', newline='\n').write(''.join(w + '\n' for w in ev.warnings))
    plan[n] = (kind, tf); deps[n] = dl

# build order (ProjectReferences between implementation assemblies)
order, seen = [], set()
def visit(n, stack=()):
    if n in seen: return
    if n in stack: raise SystemExit('cycle ' + ' -> '.join(stack + (n,)))
    for x in deps.get(n, []):
        if x in plan: visit(x, stack + (n,))
    seen.add(n); order.append(n)
for n in sorted(plan, key=lambda n: (plan[n][0] == 'shim', n)): visit(n)
with open(out + '/plan.tsv', 'w', newline='\n') as f:
    for n in order:
        f.write('\t'.join([n, plan[n][0], plan[n][1], ';'.join(deps[n])]) + '\n')
with open(out + '/skipped.tsv', 'w', newline='\n') as f:
    for n, why in skipped: f.write('%s\t%s\n' % (n, why))
missing = sorted(set(x for n in plan for x in deps[n] if x not in plan))
print('%d assemblies planned, %d skipped; non-framework project references: %s' % (len(order), len(skipped), ' '.join(missing) or '-'))
