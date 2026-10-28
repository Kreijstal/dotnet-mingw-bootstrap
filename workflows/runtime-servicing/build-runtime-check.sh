#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "${1:?usage: build-runtime-check.sh PREPARED_ROOT NEW_OUTPUT}" && pwd)
mkdir "${2:?missing new output}"; out=$(cd "$2" && pwd)
export PATH=/clang64/bin:/usr/bin:$PATH
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_ROOT_X64=$DOTNET_ROOT
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_MULTILEVEL_LOOKUP=0
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/cache")
cp "$here/RuntimeCheck.cs" "$out/"
/usr/bin/python3 - "$root" "$out" <<'PY'
from pathlib import Path
import subprocess,sys
root,out=map(Path,sys.argv[1:])
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip()
args=['-nologo','-nostdlib','-target:exe','-langversion:12','-nullable:enable','-warn:4','-warnaserror+',
      '-out:'+native(out/'RuntimeCheck.dll')]
args+=['-r:'+native(p) for p in sorted((root/'m/build/refpack-8.0.31/out').glob('*.dll'))]
args+=[native(out/'RuntimeCheck.cs')]
(out/'compile.rsp').write_text('\n'.join('"'+a+'"' for a in args)+'\n')
(out/'RuntimeCheck.runtimeconfig.json').write_text('{"runtimeOptions":{"tfm":"net8.0","framework":{"name":"Microsoft.NETCore.App","version":"8.0.31"},"rollForward":"Disable"}}\n')
PY
/clang64/lib/dotnet/dotnet.exe /clang64/lib/dotnet/compiler-packs/roslyn-4.9.2/csc.dll -noconfig "@$(cygpath -am "$out/compile.rsp")"
printf 'PASS runtime execution fixture compiled against new servicing references\n'
