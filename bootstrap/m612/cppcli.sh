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
  local name=$1 text=$2 diagnostic=$3; shift 3
  printf '%s\n' "$text" > "$testdir/$name.cpp"
  rm -f "$testdir/$name.dll"
  if compile "$(cygpath -m "$testdir/$name.cpp")" -o "$(cygpath -m "$testdir/$name.dll")" "$@" > "$testdir/$name.log" 2>&1; then
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
  run_case numeric "$src/tests/numeric.cpp" $'5000000002\n5000000004\nTrue\nFalse\nFalse\nFalse\nFalse\nTrue'
  reject_case handle-arithmetic 'public ref class Box {}; int main() { Box^ b = gcnew Box(); Box^ c = b + 1; return 0; }' 'managed handle arithmetic'
  reject_case handle-increment 'public ref class Box {}; int main() { Box^ b = gcnew Box(); ++b; return 0; }' 'handles cannot be incremented'
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
var counter = Counter.Create(5);
Console.WriteLine(counter.Add(3));
if (counter.Value != 8 || typeof(Counter).BaseType != typeof(object) || typeof(Counter).GetConstructors().Length != 1 || typeof(Counter).GetProperty("Secret") != null)
    throw new Exception("incorrect generated managed API");
if (new Defaults().Value != 23 || typeof(NoDefault).GetConstructors().Length != 0)
    throw new Exception("incorrect implicit constructor API");
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
int main() { Counter^ counter = Counter::Create(4); Console::WriteLine(counter->Add(6)); counter->Current = 12; Console::WriteLine(counter->Current); String^ text = gcnew String("hello"); Console::WriteLine(text->Length); return 0; }
EOF
  run_case references "$testdir/references.cpp" $'10\n12\n5' --type Example.Counter
  reject_case bad-constructor 'using namespace System; public ref class Box { public: Box(int x) {} }; int main() { Box^ b = gcnew Box("bad"); return 0; }' 'no matching function'
  reject_case bad-downcast 'using namespace System; public ref class Base {}; public ref class Derived : Base {}; int main() { Base^ b = gcnew Base(); Derived^ d = b; return 0; }' 'cannot initialize'
  reject_case private-field 'using namespace System; public ref class Box { int hidden; }; int main() { Box^ b = gcnew Box(); b->hidden = 1; return 0; }' 'private member'
  reject_case native-managed-pointer 'using namespace System; public ref class Box {}; int main() { Box* b = gcnew Box(); return 0; }' "native '\*' pointers"
  reject_case missing-default-constructor 'public ref class B { public: B(int value) {} }; public ref class D : B {}; int main() { D^ d = gcnew D(); return 0; }' 'no matching function'
  reject_case missing-imported-constructor 'using namespace System; int main() { Console^ c = gcnew Console(); return 0; }' 'no public metadata constructor'
  log "PASS test-objects: managed identity and Clang constructor/member checking"
}
step_test-adapter-gate() {
  log "Scope lookup must distinguish a shadowed managed type name from native XOR"
  run_case scope-xor "$src/tests/scope-xor.cpp" '4'
}
step_test-properties() {
  run_case properties "$src/tests/properties.cpp" $'set\nget\n4\nreceiver\nget\nset\nget\n6\n7\nproperty finally\n2'
}
step_test-virtuals() {
  run_case virtuals "$src/tests/virtuals.cpp" $'7\n7\n9\n6\n17\nTrue'
  mkdir -p "$testdir/virtual-consumer"
  cat > "$testdir/virtual-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="virtuals"><HintPath>../virtuals.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/virtual-consumer/Program.cs" <<'EOF'
using System;
using Example;
Base counter = new Override(2);
if (counter.Read() != 42 || ((IRead)counter).Read() != 42 || counter.Scale() != 6 ||
    !typeof(Base).IsAbstract || !typeof(IRead).IsInterface || !typeof(Base).GetMethod("Fixed").IsFinal)
    throw new Exception("incorrect virtual/interface metadata");
((IWrite)counter).Write(12);
Console.WriteLine("dispatch metadata OK");
class Override : Counter {
    public Override(int value) : base(value) {}
    public override int Read() => 42;
}
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/virtual-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/virtual-consumer/bin/Debug/net8.0/consumer.dll")"
  cat > "$testdir/virtual-reference.cpp" <<'EOF'
#using "virtuals.dll"
using namespace System;
int main() {
    Example::Counter^ counter = gcnew Example::Counter(13);
    Example::IRead^ reader = counter;
    Console::WriteLine(reader->Read());
    return 0;
}
EOF
  run_case virtual-reference "$testdir/virtual-reference.cpp" '13' --type Example.Counter --type Example.Base --type Example.IRead
  reject_case incomplete-interface 'public interface class I { int Read(); }; public ref class C : I {}; int main() { C^ c = gcnew C(); return 0; }' 'abstract'
  reject_case invalid-override 'public ref class C { public: virtual int Read() override { return 1; } }; int main() { return 0; }' 'does not override'
  reject_case sealed-override 'public ref class B { public: virtual int Read() sealed { return 1; } }; public ref class D : B { public: virtual int Read() override { return 2; } }; int main() { return 0; }' 'final'
  reject_case implicit-class-override 'public ref class B { public: virtual int Read() { return 1; } }; public ref class D : B { public: virtual int Read() { return 2; } }; int main() { return 0; }' 'explicit override'
  reject_case qualified-abstract 'public ref class B { public: virtual int Read() abstract; }; public ref class D : B { public: virtual int Read() override { return B::Read(); } }; int main() { return 0; }' 'cannot target an abstract'
  reject_case destructor 'public ref class B { public: ~B() {} }; int main() { return 0; }' 'destructors/finalizers'
  reject_case sealed-base 'public ref class B sealed {}; public ref class D : B {}; int main() { return 0; }' 'final'
}
step_test-arrays() {
  run_case arrays "$src/tests/arrays.cpp" $'3\n0\n4\nreceiver\nindex\n5\nreceiver\nindex\n5\n6\nTrue\n9\n3\nl\n6\nFalse\nTrue'
  mkdir -p "$testdir/array-consumer"
  cat > "$testdir/array-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="arrays"><HintPath>../arrays.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/array-consumer/Program.cs" <<'EOF'
using System;
int[] values = Arrays.Make(3);
if (values.Length != 3 || values[1] != 0) throw new Exception("incorrect array initialization");
Arrays.Write(values, 1, 11);
GC.Collect(); GC.WaitForPendingFinalizers();
if (Arrays.Read(values, 1) != 11) throw new Exception("incorrect array storage");
try { Arrays.Read(values, 3); throw new Exception("missing bounds check"); }
catch (IndexOutOfRangeException) {}
try { Arrays.Read(null, 0); throw new Exception("missing null check"); }
catch (NullReferenceException) {}
Console.WriteLine("array metadata and bounds OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/array-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/array-consumer/bin/Debug/net8.0/consumer.dll")"
  cat > "$testdir/array-reference.cpp" <<'EOF'
#using "arrays.dll"
using namespace System;
int main() { array<int>^ values = Arrays::Make(2); Arrays::Write(values, 1, 15); Console::WriteLine(Arrays::Read(values, 1)); values[0] = 21; Console::WriteLine(values[0]); return 0; }
EOF
  run_case array-reference "$testdir/array-reference.cpp" $'15\n21' --type Arrays
  reject_case array-rank 'int main() { array<int, 2>^ a = gcnew array<int, 2>(3); return 0; }' 'rank-one'
  reject_case array-element 'int main() { array<int>^ a = gcnew array<int>(3); a[0] = "bad"; return 0; }' 'incompatible'
}
step_test-exceptions() {
  run_case exceptions "$src/tests/exceptions.cpp" $'try\nfinally\n7\ntry\ncatch\nfinally\n9\nrethrow\nall\n13\n0\n1\n11\nonly finally\n21' --type System.Exception --type System.InvalidOperationException
  mkdir -p "$testdir/exception-consumer"
  cat > "$testdir/exception-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="exceptions"><HintPath>../exceptions.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/exception-consumer/Program.cs" <<'EOF'
using System;
try { Errors.Fail(); throw new Exception("missing managed throw"); }
catch (InvalidOperationException) {}
if (Errors.Bounds(new int[1]) != 11) throw new Exception("missing runtime exception handler");
var regions = typeof(Errors).GetMethod("Flow").GetMethodBody().ExceptionHandlingClauses;
if (regions.Count != 2 || regions[1].Flags != System.Reflection.ExceptionHandlingClauseOptions.Finally)
    throw new Exception("incorrect catch/finally table");
Console.WriteLine("exception metadata OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/exception-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/exception-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case return-finally 'int main() { try { return 1; } finally { return 2; } }' 'return cannot leave'
  reject_case native-exception 'int main() { try { throw 1; } catch (int value) { return value; } }' 'only managed handle'
  reject_case rethrow-outside-catch 'int main() { throw; }' 'rethrow requires'
}
step_test-interop() {
  log "MinGW g++ native C++ DLL -> generated CLR P/Invoke -> source-built CoreCLR"
  PATH=/ucrt64/bin:$PATH /ucrt64/bin/g++.exe -std=c++17 -shared -O2 -static-libgcc -static-libstdc++ -DCPPCLI_NATIVE_BUILD \
    "$src/tests/native.cpp" -o "$testdir/cppcli-native.dll"
  run_case interop "$src/tests/interop.cpp" $'13\n10000000004\nTrue' --native-library cppcli-native.dll
  mkdir -p "$testdir/native-consumer"
  cat > "$testdir/native-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="interop"><HintPath>../interop.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/native-consumer/Program.cs" <<'EOF'
using System;
using System.Reflection;
using System.Runtime.InteropServices;
if (NativeBridge.Sum(7, 6) != 13 || NativeBridge.Scale(5000000002L) != 10000000004L || NativeBridge.Half(7.0) != 3.5)
    throw new Exception("incorrect native ABI bridge");
var import = typeof(NativeBridge).Assembly.ManifestModule.GetMethod("NativeSum").GetCustomAttribute<DllImportAttribute>();
if (import.Value != "cppcli-native.dll" || import.EntryPoint != "NativeSum" || import.CallingConvention != CallingConvention.Cdecl)
    throw new Exception("incorrect native import metadata");
Console.WriteLine("native bridge metadata OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/native-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  cp "$testdir/cppcli-native.dll" "$testdir/native-consumer/bin/Debug/net8.0/"
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/native-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case native-bool 'extern "C" bool NativeFlag(); int main() { NativeFlag(); return 0; }' 'bool and managed/pointer marshalling' --native-library cppcli-native.dll
  reject_case native-variadic 'extern "C" int NativePrint(int value, ...); int main() { return NativePrint(1, 2); }' 'non-variadic' --native-library cppcli-native.dll
  reject_case unbound-native 'extern "C" int NativeSum(int, int); int main() { return NativeSum(1, 2); }' 'without an emitted definition'
}
step_test-enums() {
  run_case enums "$src/tests/enums.cpp" $'2\n-5\n5000000002\n5\n1' --type System.DayOfWeek
  mkdir -p "$testdir/enum-consumer"
  cat > "$testdir/enum-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="enums"><HintPath>../enums.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/enum-consumer/Program.cs" <<'EOF'
using System;
using Example;
if (Enums.Echo(Shade.Bright) != Shade.Bright || Enums.Number(Shade.Dark) != 1 || Enums.Day() != DayOfWeek.Friday ||
    Enum.GetUnderlyingType(typeof(Shade)) != typeof(byte) || (long)Signed.Negative != -5 || (long)Signed.Positive != 5000000002L)
    throw new Exception("incorrect enum metadata or values");
Console.WriteLine("enum metadata OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/enum-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/enum-consumer/bin/Debug/net8.0/consumer.dll")"
  cat > "$testdir/enum-reference.cpp" <<'EOF'
#using "enums.dll"
using namespace System;
int main() { Console::WriteLine(Example::Enums::Number(Example::Shade::Bright)); return 0; }
EOF
  run_case enum-reference "$testdir/enum-reference.cpp" '2' --type Example.Enums --type Example.Shade
  reject_case enum-overflow 'public enum class Small : unsigned char { Value = 256 }; int main() { return 0; }' 'cannot be narrowed'
}
step_test-values() {
  run_case values "$src/tests/values.cpp" $'7\n3\n5\n13\n3\n13\n10\n12\n9\n0\n9\n42'
  mkdir -p "$testdir/value-consumer"
  cat > "$testdir/value-consumer/value-consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="values"><HintPath>../values.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/value-consumer/Program.cs" <<'EOF'
using System;
using Example;
var p = new Point(3, 4);
var copy = p;
copy.Move(2);
if (!typeof(Point).IsValueType || p.X != 3 || copy.X != 5 || p.Sum() != 7 || Values.ByValue(p) != 13 || p.X != 3 || Values.Echo(p).Y != 4)
    throw new Exception("incorrect value copy/method metadata");
var h = new Holder();
h.Position.Move(5);
if (h.Position.X != 7 || default(Point).Sum() != 0) throw new Exception("incorrect value fields/default");
Console.WriteLine("value metadata and copy semantics OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/value-consumer/value-consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/value-consumer/bin/Debug/net8.0/value-consumer.dll")"
  cat > "$testdir/value-reference.cpp" <<'EOF'
#using "values.dll"
using namespace System;
int main() { Example::Point p = Example::Values::Make(3); p.Move(4); Console::WriteLine(p.X); Console::WriteLine(p.Sum()); Object^ box = safe_cast<Object^>(p); Example::Point q = safe_cast<Example::Point>(box); Console::WriteLine(q.Y); return 0; }
EOF
  run_case value-reference "$testdir/value-reference.cpp" $'7\n11\n4' --type Example.Point --type Example.Values
  cat > "$testdir/boxing-exceptions.cpp" <<'EOF'
using namespace System;
int main() { Object^ p = safe_cast<Object^>(1); try { double d = safe_cast<double>(p); } catch (InvalidCastException^ e) { Console::WriteLine(1); } Object^ n = nullptr; try { int i = safe_cast<int>(n); } catch (NullReferenceException^ e) { Console::WriteLine(2); } return 0; }
EOF
  run_case boxing-exceptions "$testdir/boxing-exceptions.cpp" $'1\n2' --type System.InvalidCastException --type System.NullReferenceException
  reject_case value-base 'public value struct Bad : System::Object {}; int main() { return 0; }' 'value types with bases'
}
for step in $steps; do "step_$step"; done

