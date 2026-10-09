#!/usr/bin/env python3
"""csproj_items.py: list the Compile/EmbeddedResource items of an SDK-style
.csproj for one target framework, without MSBuild.

We cannot run MSBuild yet (that is what we are bootstrapping), but the
dotnet/runtime, msbuild and roslyn projects list their sources explicitly
with simple conditions.  This evaluates exactly that subset:

  * <PropertyGroup> properties (in document order, with conditions),
  * <ItemGroup> Compile / EmbeddedResource / Reference / PackageReference /
    ProjectReference Include/Remove (with conditions, ** wildcards),
  * <Import Project="*.projitems"> shared projects (recursively),
  * conditions with == != and or ! ( ) and the functions
    IsTargetFrameworkCompatible, GetTargetFrameworkIdentifier,
    GetTargetPlatformIdentifier, Exists, HasTrailingSlash.

usage: csproj_items.py <project> -p Name=Value ... [--kind Compile]
       [--define] (print DefineConstants instead)
Unknown properties evaluate to ''.  Output: one path per line, relative to
the current directory if possible.
"""
import os, re, sys, glob, argparse
import xml.etree.ElementTree as ET

def strip_ns(tag):
    return tag.split('}', 1)[-1]

TFM_RE = re.compile(r'^(net|netcoreapp|netstandard)(\d+)\.?(\d*)')

def tfm_info(tfm):
    """('.NETCoreApp'|'.NETStandard'|'.NETFramework', (major, minor))"""
    tfm = tfm.split('-')[0]
    m = re.match(r'^netstandard(\d+)\.(\d+)$', tfm)
    if m: return '.NETStandard', (int(m.group(1)), int(m.group(2)))
    m = re.match(r'^netcoreapp(\d+)\.(\d+)$', tfm)
    if m: return '.NETCoreApp', (int(m.group(1)), int(m.group(2)))
    m = re.match(r'^net(\d+)\.(\d+)$', tfm)
    if m: return '.NETCoreApp', (int(m.group(1)), int(m.group(2)))
    m = re.match(r'^net(\d)(\d)(\d?)$', tfm)
    if m: return '.NETFramework', tuple(int(x) for x in m.groups() if x)
    return '', (0, 0)

def tfm_compatible(target, candidate):
    """IsTargetFrameworkCompatible(target, candidate): can target consume candidate?"""
    ti, tv = tfm_info(target); ci, cv = tfm_info(candidate)
    if ti == ci: return tv >= cv
    if ci == '.NETStandard':
        if ti == '.NETCoreApp': return cv <= (2, 1)
        if ti == '.NETFramework': return cv <= (2, 0) and tv >= (4, 6, 1)
    return False

class Evaluator:
    def __init__(self, props, globals_=()):
        self.props = dict(props)
        # -p properties behave like MSBuild global properties: projects cannot override them
        self.globals = set(globals_)
        self.items = {}   # kind -> list of (path, metadata)
        self.warnings = []

    # ---- property expansion -------------------------------------------
    def expand(self, s, base):
        if s is None: return ''
        def fn(m):
            inner = m.group(1).strip()
            if re.match(r'^[A-Za-z_][\w.]*$', inner):
                return self.props.get(inner, self.props.get(inner.lower(), ''))
            return self.call(inner, base)
        prev = None
        while prev != s:
            prev = s
            s = re.sub(r'\$\(([^()]*(?:\([^()]*\)[^()]*)*)\)', fn, s)
        return s

    def call(self, expr, base):
        m = re.match(r"^\[MSBuild\]::(\w+)\((.*)\)$", expr, re.S)
        if m:
            name, args = m.group(1), [a.strip().strip("'") for a in split_args(m.group(2))]
            args = [self.expand(a, base) for a in args]
            if name == 'IsTargetFrameworkCompatible': return str(tfm_compatible(args[0], args[1])).lower()
            if name == 'GetTargetFrameworkIdentifier': return tfm_info(args[0])[0]
            if name == 'GetTargetFrameworkVersion':
                return '.'.join(str(x) for x in tfm_info(args[0])[1])
            if name == 'GetTargetPlatformIdentifier':
                return args[0].split('-')[1] if '-' in args[0] else ''
            if name == 'NormalizeDirectory' or name == 'NormalizePath':
                p = os.path.join(*args) if args else ''
                return p.replace('\\', '/') + ('/' if name == 'NormalizeDirectory' else '')
            if name in ('GetDirectoryNameOfFileAbove', 'GetPathOfFileAbove'):
                d = os.path.abspath(os.path.join(base, args[0].replace('\\', '/')) if name == 'GetDirectoryNameOfFileAbove' else base)
                f = args[1] if name == 'GetDirectoryNameOfFileAbove' else args[0]
                while True:
                    if os.path.exists(os.path.join(d, f)):
                        return d if name == 'GetDirectoryNameOfFileAbove' else os.path.join(d, f)
                    nd = os.path.dirname(d)
                    if nd == d: return ''
                    d = nd
            if name == 'IsOSPlatform': return str(args[0].lower() == 'windows').lower()
            if name == 'IsOsPlatform': return str(args[0].lower() == 'windows').lower()
            if name == 'ValueOrDefault': return args[0] if args[0] else args[1]
            m2 = re.match(r'^Version(GreaterThanOrEquals|GreaterThan|LessThanOrEquals|LessThan|Equals|NotEquals)$', name)
            if m2:
                a, b = (tuple(int(x) for x in re.findall(r'\d+', v)) for v in args[:2])
                n = max(len(a), len(b)); a += (0,) * (n - len(a)); b += (0,) * (n - len(b))
                op = m2.group(1)
                r = {'GreaterThanOrEquals': a >= b, 'GreaterThan': a > b, 'LessThanOrEquals': a <= b,
                     'LessThan': a < b, 'Equals': a == b, 'NotEquals': a != b}[op]
                return str(r).lower()
        m = re.match(r"^(\w[\w.]*)\.(\w+)\((.*)\)$", expr)
        if m and m.group(1) in self.props:
            v = self.props[m.group(1)]
            meth = m.group(2); args = [a.strip().strip("'") for a in split_args(m.group(3))]
            if meth == 'Contains': return str(args[0] in v).lower()
            if meth == 'StartsWith': return str(v.startswith(args[0])).lower()
            if meth == 'EndsWith': return str(v.endswith(args[0])).lower()
            if meth == 'ToLowerInvariant' or meth == 'ToLower': return v.lower()
        self.warnings.append('unevaluated $(%s)' % expr)
        return ''

    # ---- conditions -----------------------------------------------------
    def cond(self, c, base):
        if c is None or not c.strip(): return True
        toks = tokenize(c)
        self._t, self._i, self._base = toks, 0, base
        try:
            r = self._or()
        except Exception as e:
            self.warnings.append('condition %r: %s -> false' % (c, e)); return False
        return r

    def _peek(self): return self._t[self._i] if self._i < len(self._t) else None
    def _next(self): t = self._peek(); self._i += 1; return t
    def _or(self):
        v = self._and()
        while self._peek() and self._peek().lower() == 'or':
            self._next(); r = self._and(); v = v or r
        return v
    def _and(self):
        v = self._not()
        while self._peek() and self._peek().lower() == 'and':
            self._next(); r = self._not(); v = v and r
        return v
    def _not(self):
        if self._peek() == '!':
            self._next(); return not self._not()
        return self._cmp()
    def _cmp(self):
        a = self._atom()
        op = self._peek()
        if op in ('==', '!=', '<', '>', '<=', '>='):
            self._next(); b = self._atom()
            if op in ('==', '!='):
                r = a.lower() == b.lower(); return r if op == '==' else not r
            fa, fb = num(a), num(b)
            return {'<': fa < fb, '>': fa > fb, '<=': fa <= fb, '>=': fa >= fb}[op]
        return truthy(a)
    def _atom(self):
        t = self._next()
        if t == '(':
            v = self._or(); assert self._next() == ')'; return 'true' if v else 'false'
        if t.startswith("'"):
            return self.expand(t[1:-1], self._base)
        if t.startswith('$('):
            return self.expand(t, self._base)
        m = re.match(r'^(Exists|HasTrailingSlash)$', t, re.I)
        if m:
            assert self._next() == '('; arg = self._next(); assert self._next() == ')'
            v = self.expand(arg.strip("'"), self._base)
            if t.lower() == 'exists':
                return 'true' if os.path.exists(os.path.join(self._base, v.replace('\\', '/'))) else 'false'
            return 'true' if v.endswith(('/', '\\')) else 'false'
        return t

    # ---- project walking ------------------------------------------------
    def load(self, path, kinds, follow=False):
        self.follow = follow
        base = os.path.dirname(os.path.abspath(path))
        self.props.setdefault('MSBuildProjectDirectory', base)
        saved = (self.props.get('MSBuildThisFileDirectory'), self.props.get('MSBuildThisFile'))
        self.props['MSBuildThisFileDirectory'] = base + '/'
        self.props['MSBuildThisFileFullPath'] = os.path.abspath(path)
        root = ET.parse(path).getroot()
        self.walk(root, base, kinds)
        self.props['MSBuildThisFileDirectory'], _ = saved[0] or '', saved[1]

    def walk(self, node, base, kinds):
        for el in node:
            tag = strip_ns(el.tag)
            if not self.cond(el.get('Condition'), base): continue
            if tag in ('PropertyGroup',):
                for p in el:
                    if not isinstance(p.tag, str): continue
                    if not self.cond(p.get('Condition'), base): continue
                    if strip_ns(p.tag) in self.globals: continue
                    self.props[strip_ns(p.tag)] = self.expand((p.text or '').strip(), base)
            elif tag == 'ItemGroup':
                for it in el:
                    if not isinstance(it.tag, str): continue
                    k = strip_ns(it.tag)
                    if not self.cond(it.get('Condition'), base): continue
                    lst = self.items.setdefault(k, [])
                    meta = {strip_ns(m.tag): self.expand((m.text or '').strip(), base) for m in it if isinstance(m.tag, str)}
                    for a, v in it.attrib.items():
                        if a not in ('Include', 'Exclude', 'Remove', 'Update', 'Condition'): meta[a] = self.expand(v, base)
                    if it.get('Include') is not None:
                        exc = set()
                        if it.get('Exclude') is not None:
                            exc = set(os.path.normpath(p) for p in self.paths(it.get('Exclude'), base, True))
                        for p in self.paths(it.get('Include'), base, k in kinds):
                            if os.path.normpath(p) not in exc: lst.append((p, meta))
                    if it.get('Remove') is not None:
                        rem = set(os.path.normpath(p) for p in self.paths(it.get('Remove'), base, True))
                        self.items[k] = [x for x in lst if os.path.normpath(x[0]) not in rem]
                    if it.get('Update') is not None:
                        upd = set(os.path.normpath(p) for p in self.paths(it.get('Update'), base, True))
                        self.items[k] = [(x[0], {**x[1], **meta}) if os.path.normpath(x[0]) in upd else x for x in lst]
            elif tag == 'Import':
                proj = self.expand(el.get('Project'), base).replace('\\', '/')
                full = os.path.normpath(os.path.join(base, proj))
                if el.get('Sdk'): continue
                if proj.endswith('.projitems') or getattr(self, 'follow', False):
                    if any(c in full for c in '*?'):
                        for g in sorted(glob.glob(full, recursive=True)):
                            fl = self.follow; self.load(g, kinds, fl); self.follow = fl
                    elif os.path.exists(full):
                        fl = self.follow; self.load(full, kinds, fl); self.follow = fl
                    else: self.warnings.append('missing import ' + full)
            elif tag in ('Choose',):
                for w in el:
                    wt = strip_ns(w.tag)
                    if wt == 'When' and self.cond(w.get('Condition'), base):
                        self.walk(w, base, kinds); break
                    if wt == 'Otherwise':
                        self.walk(w, base, kinds); break
            elif tag == 'Target':
                continue

    def paths(self, spec, base, is_file):
        out = []
        for part in self.expand(spec, base).split(';'):
            part = part.strip().replace('\\', '/')
            if not part: continue
            if not is_file:
                out.append(part); continue
            full = part if os.path.isabs(part) else os.path.join(base, part)
            if any(ch in full for ch in '*?'):
                out.extend(sorted(glob.glob(full, recursive=True)))
            else:
                out.append(os.path.normpath(full))
        return out

def split_args(s):
    out, depth, cur, q = [], 0, '', False
    for ch in s:
        if ch == "'": q = not q
        if not q and ch == '(': depth += 1
        if not q and ch == ')': depth -= 1
        if not q and depth == 0 and ch == ',':
            out.append(cur); cur = ''; continue
        cur += ch
    if cur.strip(): out.append(cur)
    return out

def tokenize(c):
    toks, i = [], 0
    while i < len(c):
        ch = c[i]
        if ch.isspace(): i += 1; continue
        if ch == "'":
            j = c.index("'", i + 1); toks.append(c[i:j + 1]); i = j + 1; continue
        if c.startswith('$(', i):
            d, j = 0, i
            while True:
                if c[j] == '(': d += 1
                elif c[j] == ')':
                    d -= 1
                    if d == 0: break
                j += 1
            toks.append(c[i:j + 1]); i = j + 1; continue
        for op in ('==', '!=', '<=', '>=', '<', '>', '!', '(', ')'):
            if c.startswith(op, i): toks.append(op); i += len(op); break
        else:
            m = re.match(r'[\w.]+', c[i:])
            if not m: raise ValueError('bad token at %r' % c[i:])
            toks.append(m.group(0)); i += len(m.group(0))
    return toks

def truthy(v): return v.strip().lower() == 'true'
def num(v):
    try: return float(v)
    except ValueError: return 0.0

def main():
    # native Windows python would print CRLF; consumers are bash scripts
    sys.stdout.reconfigure(newline='\n')
    ap = argparse.ArgumentParser()
    ap.add_argument('project')
    ap.add_argument('-p', action='append', default=[])
    ap.add_argument('--kind', default='Compile')
    ap.add_argument('--define', action='store_true')
    ap.add_argument('--prop', action='append', default=[])
    ap.add_argument('--meta', default=None, help='print item metadata NAME after a tab')
    ap.add_argument('--pre', action='append', default=[], help='props file evaluated before the project (imports followed)')
    ap.add_argument('--post', action='append', default=[], help='targets file evaluated after the project (imports followed)')
    ap.add_argument('--default-items', action='store_true',
                    help='SDK default Compile glob (**/*.cs minus bin/obj) unless the project sets EnableDefaultCompileItems=false')
    a = ap.parse_args()
    props = {}
    for kv in a.p:
        k, _, v = kv.partition('='); props[k] = v
    if 'TargetFramework' in props:
        ident, ver = tfm_info(props['TargetFramework'])
        props.setdefault('TargetFrameworkIdentifier', ident)
        props.setdefault('TargetFrameworkVersion', 'v' + '.'.join(str(x) for x in (ver + (0,))[:max(2, len(ver))]))
    ev = Evaluator(props, [kv.partition('=')[0] for kv in a.p])
    if a.default_items:
        text = open(a.project, encoding='utf-8-sig').read()
        if not re.search(r'<EnableDefault(Compile)?Items>\s*false', text, re.I):
            base = os.path.dirname(os.path.abspath(a.project))
            for f in sorted(glob.glob(os.path.join(base, '**', '*.cs'), recursive=True)):
                r = os.path.relpath(f, base).replace('\\', '/')
                if r.split('/')[0] in ('bin', 'obj'): continue
                ev.items.setdefault('Compile', []).append((f, {}))
    kinds = {'Compile', 'EmbeddedResource', 'None', 'Content'}
    ev.props.setdefault('MSBuildProjectDirectory', os.path.dirname(os.path.abspath(a.project)))
    ev.props.setdefault('MSBuildProjectName', os.path.splitext(os.path.basename(a.project))[0])
    ev.props.setdefault('MSBuildProjectFile', os.path.basename(a.project))
    ev.props.setdefault('MSBuildProjectExtension', os.path.splitext(a.project)[1])
    for f in a.pre: ev.load(f, kinds, True)
    ev.load(a.project, kinds, not os.environ.get('CSPROJ_NO_FOLLOW'))
    for f in a.post: ev.load(f, kinds, True)
    for w in ev.warnings: print('warning: ' + w, file=sys.stderr)
    if a.define:
        print(ev.props.get('DefineConstants', '')); return
    if a.prop:
        for p in a.prop: print('%s=%s' % (p, ev.props.get(p, '')))
        return
    seen = set()
    for p, meta in ev.items.get(a.kind, []):
        key = os.path.normpath(p)
        if key in seen: continue
        seen.add(key)
        rel = os.path.relpath(p) if os.path.isabs(p) else p
        rel = rel.replace('\\', '/')
        print(rel + ('\t' + meta.get(a.meta, '') if a.meta else ''))

if __name__ == '__main__':
    main()
