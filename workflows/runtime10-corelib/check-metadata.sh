#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: check-metadata.sh PREPARED_ROOT STAGED_MANAGED_OUTPUT}" && pwd)
tested=$(cd "$2" && pwd)
out="$root/corelib10-1/corelib"
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/tools/metadata/Build.csproj")" \
  -c Release --configfile "$(cygpath -am "$out/tools/NuGet.config")" \
  -p:UseSharedCompilation=false -v:minimal > "$out/metadata-build-final.log" 2>&1
for variant in raw tested; do
  assembly="$out/lib/System.Private.CoreLib.dll"
  if [ "$variant" = tested ]; then assembly="$tested/native CLR10 λ/System.Private.CoreLib.dll"; fi
  /clang64/lib/dotnet/dotnet.exe \
    "$(cygpath -am "$out/tools/metadata/bin/Release/net8.0/CoreLib10.MetadataCheck.dll")" \
    "$(cygpath -am "$assembly")" \
    "$(cygpath -am "$root/r/src/libraries/System.Private.CoreLib/src/Resources/Strings.resx")" \
    > "$out/metadata-$variant.json"
done
/usr/bin/python3 - "$out" <<'PY'
from pathlib import Path
import json,sys
out=Path(sys.argv[1]);raw=json.loads((out/'metadata-raw.json').read_text());tested=json.loads((out/'metadata-tested.json').read_text())
assert tested['internal_calls_with_body']==0
assert raw['internal_calls']==tested['internal_calls']==215
print('PASS raw/tested CoreLib10 identity, zero assembly refs, all1421 resource values and InternalCall metadata')
PY
