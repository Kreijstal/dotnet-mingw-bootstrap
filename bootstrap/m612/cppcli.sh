#!/bin/bash
# Build the adapter/Clang AST frontend; all managed tools use the source-built SDK.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
src=$here/cppcli
build=${CPPCLI_BUILD:-$M612/build/cppcli}
testdir=${CPPCLI_TESTDIR:-$M612/test/cppcli}
export CPPCLI_SDK=${CPPCLI_SDK:-$M612/dotnet-sdk-8.0.131-stage2}
export CPPCLI_ILASM=${CPPCLI_ILASM:-$M612/build/coreclr-8.0.0/cmake/ilasm/ilasm.exe}
export CPPCLI_CLANG=/clang64/bin/clang++.exe
steps=${*:-build test-core}
mkdir -p "$build" "$testdir"

step_build() {
  [ "$MSYSTEM" = CLANG64 ] || die "build this Clang plugin under MSYSTEM=CLANG64"
  log "Build metadata reader with source-built SDK"
  cat > "$build/NuGet.config" <<'EOF'
<configuration><packageSources><clear /></packageSources></configuration>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$src/metadata/metadata.csproj")" -c Release -nologo \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false \
    "-p:BaseIntermediateOutputPath=$(cygpath -m "$build/metadata-obj")/" \
    "-p:OutputPath=$(cygpath -m "$build/metadata")/" -p:NuGetAudit=false
  log "Build Clang token adapter and AST emitter"
  /clang64/bin/python.exe "$src/fetch-clang-parse.py" "$M612/src/clang-parse-22.1.8" "$build/clang-parse"
  /clang64/bin/cmake.exe -S "$src" -B "$build" -GNinja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=/clang64/bin/clang.exe -DCMAKE_CXX_COMPILER=/clang64/bin/clang++.exe \
    -DLLVM_DIR=/clang64/lib/cmake/llvm -DCPPCLI_PARSE_DIR="$build/clang-parse"
  /clang64/bin/cmake.exe --build "$build" --target cli_ast -j2
}
compile() {
  /clang64/bin/python.exe "$build/cli-clang" --sdk "$(cygpath -m "$CPPCLI_SDK")" \
    --clang "$(cygpath -m "$CPPCLI_CLANG")" --ilasm "$(cygpath -m "$CPPCLI_ILASM")" "$@"
}
run_case() {
  local name=$1 input=$2 expected=$3; shift 3
  compile "$(cygpath -m "$input")" -o "$(cygpath -m "$testdir/$name.dll")" "$@"
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/$name.dll")" > "$testdir/$name.stdout"
  tr -d '\r' < "$testdir/$name.stdout" > "$testdir/$name.actual"
  printf '%s\n' "$expected" > "$testdir/$name.expected"
  cmp "$testdir/$name.expected" "$testdir/$name.actual" || die "runtime mismatch in $name"
  [ -s "$testdir/$name.dll.work/ast.json" ] || die "missing Clang AST evidence"
  printf 'PASS %s\n' "$name"
}
reject_case() {
  local name=$1 text=$2 diagnostic=$3
  printf '%s\n' "$text" > "$testdir/$name.cpp"
  rm -f "$testdir/$name.dll"
  if compile "$(cygpath -m "$testdir/$name.cpp")" -o "$(cygpath -m "$testdir/$name.dll")" > "$testdir/$name.log" 2>&1; then
    die "accepted invalid/unsupported input: $name"
  fi
  [ ! -e "$testdir/$name.dll" ] || die "produced assembly for rejected input: $name"
  grep -q "$diagnostic" "$testdir/$name.log" || { cat "$testdir/$name.log"; die "wrong rejection diagnostic: $name"; }
  printf 'PASS reject %s\n' "$name"
}
step_test-core() {
  log "Actual Clang AST -> source-built ilasm -> source-built CoreCLR"
  run_case hello "$here/cli-poc/hello.cpp" 'Hello from C++/CLI through a GCC plugin!'
  run_case control "$src/tests/control.cpp" $'13\ncontrol OK\nUnicode: λ 😀'
  run_case macros "$src/tests/control.cpp" $'8\ncontrol OK\nUnicode: λ 😀' -D LIMIT=5
  run_case overloads "$src/tests/overloads.cpp" $'5\n10\nFalse\ntext'
  run_case templates "$src/tests/templates.cpp" $'14\n5\n7'
  run_case branches "$src/tests/branches.cpp" $'False\nTrue\nTrue\n1\n12\n25'
  reject_case unknown 'using namespace System; int main() { int n = missing; return n; }' 'undeclared identifier'
  reject_case mismatch 'using namespace System; int main() { int n = "wrong"; return n; }' 'cannot initialize'
  reject_case outside 'using namespace System; int main() { switch (1) { default: return 0; } }' 'unsupported AST statement'
  reject_case missing-return 'using namespace System; int broken() { int n = 0; } int main() { return broken(); }' 'does not return'
  log "PASS test-core: Clang parsing, metadata overloads, control flow, macros/includes, Unicode, negative checks"
}
step_test-objects() {
  log "Managed constructors, fields, inheritance, handles and member calls"
  run_case objects "$src/tests/objects.cpp" $'8\n16\nTrue\nTrue\nTrue\nTrue\nFalse\ntyped literal\n1\n2'
  run_case evaluation "$src/tests/evaluation.cpp" $'receiver\n3\nreceiver\n3\nreceiver\n5\n5'
  compile "$(cygpath -m "$src/tests/library.cpp")" --target library -o "$(cygpath -m "$testdir/counter.dll")"
  mkdir -p "$testdir/consumer"
  cat > "$testdir/consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="counter"><HintPath>../counter.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/consumer/Program.cs" <<'EOF'
using System;
using Example;
var counter = new Counter(5);
Console.WriteLine(counter.Add(3));
if (counter.Value != 8 || typeof(Counter).BaseType != typeof(object) || typeof(Counter).GetConstructors().Length != 1)
    throw new Exception("incorrect generated managed API");
counter.Current = 11;
if (counter.Current != 11 || typeof(Counter).GetProperty("Current").PropertyType != typeof(int))
    throw new Exception("incorrect generated property API");
Console.WriteLine("metadata OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/consumer/bin/Debug/net8.0/consumer.dll")" > "$testdir/consumer.stdout"
  tr -d '\r' < "$testdir/consumer.stdout" > "$testdir/consumer.actual"
  printf '%s\n' '8' 'metadata OK' > "$testdir/consumer.expected"
  cmp "$testdir/consumer.expected" "$testdir/consumer.actual"
  printf 'PASS C# consumer and metadata\n'
  cat > "$testdir/references.cpp" <<'EOF'
#if 0
#using "this-file-must-not-be-loaded.dll"
#endif
#using "counter.dll"
using namespace System;
using namespace Example;
int main() { Counter^ counter = gcnew Counter(4); Console::WriteLine(counter->Add(6)); counter->Current = 12; Console::WriteLine(counter->Current); String^ text = gcnew String("hello"); Console::WriteLine(text->Length); return 0; }
EOF
  run_case references "$testdir/references.cpp" $'10\n12\n5' --type Example.Counter
  reject_case bad-constructor 'using namespace System; public ref class Box { public: Box(int x) {} }; int main() { Box^ b = gcnew Box("bad"); return 0; }' 'no matching function'
  reject_case bad-downcast 'using namespace System; public ref class Base {}; public ref class Derived : Base {}; int main() { Base^ b = gcnew Base(); Derived^ d = b; return 0; }' 'cannot initialize'
  reject_case private-field 'using namespace System; public ref class Box { int hidden; }; int main() { Box^ b = gcnew Box(); b->hidden = 1; return 0; }' 'private member'
  reject_case native-managed-pointer 'using namespace System; public ref class Box {}; int main() { Box* b = gcnew Box(); return 0; }' "native '\*' pointers"
  log "PASS test-objects: managed identity and Clang constructor/member checking"
}
step_test-adapter-gate() {
  log "Scope lookup must distinguish a shadowed managed type name from native XOR"
  run_case scope-xor "$src/tests/scope-xor.cpp" '4'
}
step_test-properties() {
  run_case properties "$src/tests/properties.cpp" $'set\nget\n4\nreceiver\nget\nset\nget\n6\n7'
}
for step in $steps; do "step_$step"; done
