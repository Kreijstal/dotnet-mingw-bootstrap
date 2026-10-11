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
  --framework windowsforms
timeout 60 "$sdk/dotnet.exe" "$(cygpath -m "$work/hello.dll")" | tr -d '\r' > "$work/hello.stdout"
grep -qx True "$work/hello.stdout"
printf 'C++/CLI WinForms window OK\n'
