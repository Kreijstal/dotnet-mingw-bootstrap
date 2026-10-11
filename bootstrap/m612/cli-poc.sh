#!/bin/bash
# GCC-hosted C++/CLI subset -> IL -> source-built ilasm -> source-built CoreCLR.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
src=$here/cli-poc
out=${CLI_POC_OUT:-$M612/test/cli-poc}
sdk=${CLI_POC_SDK:-$M612/dotnet-sdk-8.0.131-stage2}
ilasm=${ILASM:-$M612/build/coreclr-8.0.0/cmake/ilasm/ilasm.exe}
cxx=${CLI_POC_CXX:-/ucrt64/bin/g++.exe}
mkdir -p "$out"
[ -x "$sdk/dotnet.exe" ] || die "missing source-built SDK: $sdk"
[ -x "$ilasm" ] || die "missing source-built ilasm: $ilasm"

log "Build the GCC plugin with MinGW"
"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type \
  -shared -static-libgcc -static-libstdc++ "$src/cli_poc.cpp" -o "$out/cli_poc.dll"

compile() {
  local input=$1 il=$2
  "$cxx" -fsyntax-only "-fplugin=$(cygpath -m "$out/cli_poc.dll")" \
    "-fplugin-arg-cli_poc-source=$(cygpath -m "$input")" \
    "-fplugin-arg-cli_poc-output=$(cygpath -m "$il")" "$input"
}
assemble_run() {
  local name=$1
  "$ilasm" "$(cygpath -m "$out/$name.il")" -exe -quiet -nologo \
    "-output=$(cygpath -m "$out/$name.dll")"
  cat > "$out/$name.runtimeconfig.json" <<'EOF'
{"runtimeOptions":{"tfm":"net8.0","framework":{"name":"Microsoft.NETCore.App","version":"8.0.0"}}}
EOF
  "$sdk/dotnet.exe" "$(cygpath -m "$out/$name.dll")" > "$out/$name.stdout"
}

log "Confirm stock g++ rejects managed declarations"
if "$cxx" -fsyntax-only "$src/hello.cpp" > "$out/stock.log" 2>&1; then
  die "stock g++ unexpectedly accepted the CLI input"
fi

log "Compile and run C++/CLI hello world"
compile "$src/hello.cpp" "$out/hello.il"
assemble_run hello
printf '%s\n' 'Hello from C++/CLI through a GCC plugin!' > "$out/hello.expected"
# Windows Console.WriteLine uses CRLF.
tr -d '\r' < "$out/hello.stdout" > "$out/hello.actual"
cmp "$out/hello.expected" "$out/hello.actual"
cat "$out/hello.actual"

log "Verify identifiers, string escaping and multiple statements"
cat > "$out/variant.cpp" <<'EOF'
/* The output must come from the source, not a fixed hello-world template. */
using namespace System;
public ref class Other abstract sealed {
public:
    static void Speak() {
        String^ first = gcnew String(L"A \"quoted\" path: C:\\tmp");
        String^ second = gcnew String(L"line one\nline two");
        Console::WriteLine(first);
        Console::WriteLine(second);
        Console::WriteLine("literal");
    }
};
int main() { Other::Speak(); return 0; }
EOF
compile "$out/variant.cpp" "$out/variant.il"
assemble_run variant
printf '%s\n' 'A "quoted" path: C:\tmp' 'line one' 'line two' 'literal' > "$out/variant.expected"
tr -d '\r' < "$out/variant.stdout" > "$out/variant.actual"
cmp "$out/variant.expected" "$out/variant.actual"

log "Reject unsupported constructs and unresolved managed locals"
for kind in unsupported unresolved malformed; do
  case "$kind" in
    unsupported) sed 's/Console::WriteLine(message);/Console::ReadLine();/' "$src/hello.cpp" > "$out/$kind.cpp" ;;
    unresolved) sed 's/WriteLine(message)/WriteLine(missing)/' "$src/hello.cpp" > "$out/$kind.cpp" ;;
    malformed) sed 's/return 0;/return 1;/' "$src/hello.cpp" > "$out/$kind.cpp" ;;
  esac
  rm -f "$out/$kind.il"
  if compile "$out/$kind.cpp" "$out/$kind.il" > "$out/$kind.log" 2>&1; then
    die "accepted invalid/unsupported input: $kind"
  fi
  [ ! -e "$out/$kind.il" ] || die "created IL for rejected input: $kind"
  grep -q 'cli_poc:.*line ' "$out/$kind.log" || die "missing parser diagnostic: $kind"
done
log "PASS: GCC plugin -> IL -> ilasm -> CoreCLR (hello, variant, 3 rejection checks)"
