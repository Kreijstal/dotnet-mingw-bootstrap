#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-managed-check.sh PREPARED_ROOT STAGED_OUTPUT}" && pwd)
out=$(cd "$2" && pwd)
MSYS2_ARG_CONV_EXCL=/noconfig /clang64/lib/dotnet/dotnet.exe \
  "$(cygpath -am "$root/cdac-bootstrap5/compiler/CompilerHost.dll")" \
  /noconfig "@$(cygpath -am "$out/probe.rsp")" > "$out/probe-build.log" 2>&1
/clang64/lib/dotnet/dotnet.exe \
  "$(cygpath -am "$root/corelib10-1/corelib/tools/fcall/bin/Release/net8.0/FcallRva.dll")" \
  "$(cygpath -am "$out/native CLR10 λ/System.Private.CoreLib.dll")" > "$out/fcall-rewrite.log" 2>&1
/clang64/bin/python.exe "$(cygpath -am "$(dirname "$0")/check-managed.py")" "$(cygpath -am "$out")"
