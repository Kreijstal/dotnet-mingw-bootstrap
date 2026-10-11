#!/bin/bash
set -euo pipefail
sdk=$1
compiler=$2
work=$3
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$work"
runner=("$compiler")
[[ $compiler == *.exe ]] || runner=(/clang64/bin/python.exe "$(cygpath -m "$compiler")")
"${runner[@]}" "$(cygpath -m "$here/hello.cpp")" -o "$(cygpath -m "$work/hello.dll")" \
  --sdk "$(cygpath -m "$sdk")" --clang "$(cygpath -m /clang64/bin/clang++.exe)" \
  --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" \
  --reference "$(cygpath -m "$sdk/shared/Microsoft.WindowsDesktop.App/8.0.0-bootstrap.7.0.20/System.Windows.Forms.dll")" \
  --reference "$(cygpath -m "$sdk/packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0/System.ComponentModel.Primitives.dll")" \
  --type System.Windows.Forms.Form --type System.Windows.Forms.ContainerControl \
  --type System.Windows.Forms.ScrollableControl --type System.Windows.Forms.Control \
  --type System.Windows.Forms.Application --type System.ComponentModel.Component --type System.MarshalByRefObject
/usr/bin/python3 - "$work/hello.runtimeconfig.json" <<'PY'
import json,sys
from pathlib import Path
p=Path(sys.argv[1]);data=json.loads(p.read_text());options=data['runtimeOptions'];options['frameworks']=[options.pop('framework'),{'name':'Microsoft.WindowsDesktop.App','version':'8.0.0-bootstrap.7.0.20'}];p.write_text(json.dumps(data))
PY
timeout 60 "$sdk/dotnet.exe" "$(cygpath -m "$work/hello.dll")" | tr -d '\r' > "$work/hello.stdout"
grep -qx True "$work/hello.stdout"
printf 'C++/CLI WinForms window OK\n'
