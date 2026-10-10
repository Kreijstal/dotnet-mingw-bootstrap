#!/bin/bash
# S12: the seed packages: net8.0 assemblies built by the hand-made drivers of S9-S11 (libraries.sh OOB,
# clideps-net8.sh) packed under their upstream package ids, for the dependencies whose own repos are not
# rebuilt with their project files in S12 (dotnet/runtime's out-of-band libraries, HostModel;
# ApplicationInsights).  lib/net8.0 only (no netstandard2.0 build exists of them); nuspec dependencies
# come from the assemblies' references (asminfo).  They live in feed-seed and are lifted into a repo's
# versions only when the repo lists them (S12_LIFT_SEEDS).  Runtime packages get version 8.0.0 (runtime
# v8.0.0 sources); upstream's are 8.0.x servicing builds.
# usage: s12-feed.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
OOB=$M612/oob-8.0.0/lib
FX=$M612/netcoreapp-8.0.0/lib
P8=$M612/net8-pkgs
# id|version|assembly
SEEDS="$(for f in "$OOB"/*.dll; do echo "$(basename "$f" .dll)|8.0.0|$f"; done)
$(for n in System.Text.Json System.Formats.Asn1 System.Text.Encoding.CodePages System.Text.Encodings.Web System.Diagnostics.DiagnosticSource \
           System.Threading.Channels System.Collections.Immutable System.Reflection.Metadata; do
    [ -f "$FX/$n.dll" ] && echo "$n|8.0.0|$FX/$n.dll"; done)
Microsoft.NET.HostModel|8.0.0|$P8/Microsoft.NET.HostModel.dll
Microsoft.ApplicationInsights|2.21.0|$P8/Microsoft.ApplicationInsights.dll"
T=$S12/build/seed; rm -rf "${T:?}"; mkdir -p "$T"
echo "$SEEDS" | while IFS='|' read -r i v f; do echo "$i|$v|$(m "$f")"; done > "$T/seeds.txt"
# assembly references of every seed
CSC_DOTNET=$BUILD_SDK/dotnet.exe net8tool asminfo $(echo "$SEEDS" | cut -d'|' -f3 | while read -r f; do m "$f"; done) > "$T/asminfo.txt"
python3 -S - "$T/seeds.txt" "$T/asminfo.txt" "$T" "$FEED_SEED" <<'EOPY'
import os, re, shutil, sys, zipfile
seeds_txt, info_txt, tmp, out = sys.argv[1:5]
seeds = [l.strip().split('|') for l in open(seeds_txt) if l.strip()]
ver = {s[0].lower(): (s[0], s[1]) for s in seeds}
refs, cur = {}, None
for l in open(info_txt, encoding='utf-8'):
    m = re.match(r'(\S.*?): (\S+) \S+ mvid=', l)
    if m: cur = m.group(2); refs[cur] = []; continue
    m = re.match(r'  ref (\S+) ', l)
    if m and cur: refs[cur].append(m.group(1))
T = (2000, 1, 1, 0, 0, 0)
for pid, v, path in seeds:
    deps = sorted(set(ver[r.lower()] for r in refs.get(pid, []) if r.lower() in ver and r.lower() != pid.lower()))
    dx = ''.join('        <dependency id="%s" version="%s" />\n' % d for d in deps)
    nuspec = ('<?xml version="1.0" encoding="utf-8"?>\n<package xmlns="http://schemas.microsoft.com/packaging/2013/05/nuspec.xsd">\n'
              '  <metadata>\n    <id>%s</id>\n    <version>%s</version>\n    <authors>m612</authors>\n'
              '    <description>m612 S12 seed: %s from the hand-made S9-S11 build (not built from its project file)</description>\n'
              '    <dependencies>\n      <group targetFramework="net8.0">\n%s      </group>\n    </dependencies>\n  </metadata>\n</package>\n') % (pid, v, pid, dx)
    nup = os.path.join(out, ('%s.%s.nupkg' % (pid, v)).lower())
    with zipfile.ZipFile(nup, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr(zipfile.ZipInfo(pid + '.nuspec', T), nuspec)
        zi = zipfile.ZipInfo('lib/net8.0/%s.dll' % pid, T); zi.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(zi, open(path, 'rb').read())
    print('seed %s %s (%d deps)' % (pid, v, len(deps)))
EOPY
# Microsoft.NETCore.DotNetHostResolver: the native hostfxr (corehost.sh, CLANG64) as runtimes/win-x64/native
HX=$T/hostresolver; mkdir -p "$HX/runtimes/win-x64/native"
cp "$M612/corehost-8.0.0/corehost/hostfxr.dll" "$HX/runtimes/win-x64/native/"
cat > "$HX/Microsoft.NETCore.DotNetHostResolver.nuspec" <<'EOX'
<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2013/05/nuspec.xsd">
  <metadata>
    <id>Microsoft.NETCore.DotNetHostResolver</id>
    <version>8.0.0</version>
    <authors>m612</authors>
    <description>m612 S12 seed: hostfxr.dll of corehost.sh (runtime v8.0.0, CLANG64)</description>
  </metadata>
</package>
EOX
python3 -S "$M612_TOOLS_SRC/s12nupkg.py" "$HX" "$HX/Microsoft.NETCore.DotNetHostResolver.nuspec" "$FEED_SEED"
ls "$FEED_SEED" | wc -l
