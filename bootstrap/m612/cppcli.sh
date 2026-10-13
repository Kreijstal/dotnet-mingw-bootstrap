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
  run_case language14 "$src/tests/control.cpp" $'13\ncontrol OK\nUnicode: λ 😀' --std c++14
  run_case overloads "$src/tests/overloads.cpp" $'5\n10\nFalse\ntext'
  run_case templates "$src/tests/templates.cpp" $'14\n5\n7'
  run_case system-header "$src/tests/system-header.cpp" 'system header OK'
  reject_case system-user-specialization '#include "system-header.hpp"
template<> struct SystemTrait<float> {}; int main() { return 0; }' 'cannot be specialized' -I "$(cygpath -m "$src/tests")"
  run_case numeric "$src/tests/numeric.cpp" $'5000000002\n5000000004\nTrue\nFalse\nFalse\nFalse\nFalse\nTrue'
  reject_case handle-arithmetic 'public ref class Box {}; int main() { Box^ b = gcnew Box(); Box^ c = b + 1; return 0; }' 'managed handle arithmetic'
  reject_case handle-increment 'public ref class Box {}; int main() { Box^ b = gcnew Box(); ++b; return 0; }' 'handles cannot be incremented'
  run_case branches "$src/tests/branches.cpp" $'False\nTrue\nTrue\n1\n12\n25'
  reject_case unknown 'using namespace System; int main() { int n = missing; return n; }' 'undeclared identifier'
  reject_case mismatch 'using namespace System; int main() { int n = "wrong"; return n; }' 'cannot initialize'
  reject_case outside 'using namespace System; int main() { do {} while (false); return 0; }' 'unsupported AST statement'
  reject_case missing-return 'using namespace System; int broken() { int n = 0; } int main() { return broken(); }' 'does not return'
  log "PASS test-core: Clang parsing, metadata overloads, control flow, macros/includes, Unicode, negative checks"
}
step_test-nested-indexers() {
  log "Nested imported indexers returning managed arrays"
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  run_case nested-indexers "$src/tests/nested-indexers.cpp" '42' --type 'System.Collections.Generic.List`1' \
    --reference "$(cygpath -m "${refs[-1]}/System.Collections.dll")"
  log "PASS test-nested-indexers: array getter result and single receiver/index evaluation"
}
step_test-managed-templates() {
  log "Closed managed C++ class templates and generic-owner property definitions"
  compile "$(cygpath -m "$src/tests/managed-templates.cpp")" --target library -o "$(cygpath -m "$testdir/managed-templates.dll")"
  mkdir -p "$testdir/managed-template-consumer"
  cp "$src/tests/ManagedTemplatesConsumer.cs" "$testdir/managed-template-consumer/Program.cs"
  cat > "$testdir/managed-template-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="managed-templates"><HintPath>../managed-templates.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/managed-template-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/managed-template-consumer/bin/Debug/net8.0/consumer.dll")"
  cat > "$testdir/value-equality.cpp" <<'EOF'
using namespace System;
int main() {
    IntPtr left = IntPtr(5000000002LL), right = IntPtr(5000000002LL), other = IntPtr(3);
    Console::WriteLine(left == right); Console::WriteLine(left == other); Console::WriteLine(left != other);
    UIntPtr high = UIntPtr(0xffffffffffffffffULL), same = UIntPtr(0xffffffffffffffffULL), zero = UIntPtr(0U);
    Console::WriteLine(high == same); Console::WriteLine(high == zero); Console::WriteLine(high != zero);
    Guid first = Guid("b2d9f3ec-c9fe-4a11-a2ec-d86208f7c0a2"), second = Guid("b2d9f3ec-c9fe-4a11-a2ec-d86208f7c0a2");
    Guid different = Guid("b2d9f3ec-c9fe-4a11-a2ec-d86208f7c0a3");
    Console::WriteLine(first == second); Console::WriteLine(first == different);
    return 0;
}
EOF
  run_case value-equality "$testdir/value-equality.cpp" $'True\nFalse\nTrue\nTrue\nFalse\nTrue\nTrue\nFalse' --type System.Guid
  log "PASS test-managed-templates: ordinary CLR specializations and real accessor/virtual calls"
}
step_test-guid-object-boxing() {
  log "Functional managed String constructors and value-to-Object boxing"
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local args=(--type System.Guid --type System.Globalization.CultureInfo --type 'System.Collections.Generic.KeyValuePair`2'
    --reference "$(cygpath -m "${refs[-1]}/System.Globalization.dll")" --reference "$(cygpath -m "${refs[-1]}/System.Collections.dll")")
  compile "$(cygpath -m "$src/tests/guid-object-boxing.cpp")" --target library "${args[@]}" -o "$(cygpath -m "$testdir/guid-object-boxing.dll")"
  mkdir -p "$testdir/guid-object-consumer"
  cp "$src/tests/GuidObjectBoxingConsumer.cs" "$testdir/guid-object-consumer/Program.cs"
  cat > "$testdir/guid-object-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="guid-object-boxing"><HintPath>../guid-object-boxing.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/guid-object-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/guid-object-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case value-ctor-private 'using namespace System; public value class C { C(String^ text) {} }; int main() { C value = C("private"); return 0; }' 'private constructor'
  log "PASS test-guid-object-boxing: typed constructors, CLR copies, Object identity and generic property boxing"
}
step_test-primitive-statics() {
  log "Primitive static members from source-built metadata"
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local args=(--type System.Double --type System.Single --type System.Char --type System.Int32 --type System.UInt32
    --type System.Int64 --type System.UInt64 --type System.SByte --type System.Byte --type System.Int16 --type System.UInt16
    --type System.Type --type System.Globalization.NumberStyles --type System.Globalization.CultureInfo --type System.IFormatProvider
    --reference "$(cygpath -m "${refs[-1]}/System.Globalization.dll")")
  compile "$(cygpath -m "$src/tests/primitive-statics.cpp")" --target library "${args[@]}" -o "$(cygpath -m "$testdir/primitive-statics.dll")"
  mkdir -p "$testdir/primitive-statics-consumer"
  cp "$src/tests/PrimitiveStaticsConsumer.cs" "$testdir/primitive-statics-consumer/Program.cs"
  cat > "$testdir/primitive-statics-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="primitive-statics"><HintPath>../primitive-statics.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/primitive-statics-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/primitive-statics-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case primitive-write 'using namespace System; int main() { Int32::MaxValue = 0; return 0; }' 'const-qualified' "${args[@]}"
  reject_case primitive-address 'using namespace System; int main() { const double* pointer = &Double::MinValue; return 0; }' 'no addressable CLR storage' "${args[@]}"
  reject_case primitive-unknown 'using namespace System; int main() { return Int32::Missing; }' 'no member named' "${args[@]}"
  reject_case primitive-value-shadow 'using namespace System; int main() { int Int32 = 0; return Int32::MaxValue; }' 'not a class' "${args[@]}"
  log "PASS test-primitive-statics: exact literal bits, scalar aliases, CLR methods and native lookup"
}
step_test-paramarrays() {
  log "Imported ParamArray metadata and omitted-array CLR calls"
  mkdir -p "$testdir/paramarray-fixture"
  cp "$src/tests/ParamArrayFixture.cs" "$testdir/paramarray-fixture/Fixture.cs"
  cat > "$testdir/paramarray-fixture/fixture.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><AssemblyName>paramarray-fixture</AssemblyName><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/paramarray-fixture/fixture.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  cp "$testdir/paramarray-fixture/bin/Debug/net8.0/paramarray-fixture.dll" "$testdir/"
  cp "$src/tests/paramarrays.cpp" "$testdir/paramarrays.cpp"
  run_case paramarrays "$testdir/paramarrays.cpp" $'0\n-1\n17\n33\n0\n29\n2\nTrue\nTrue\nTrue\nprefix\n40\n17\n29\n29' --type ParamArrayFixture
  reject_case paramarray-required '#using "paramarray-fixture.dll"
int main() { return ParamArrayFixture::Required(); }' 'too few arguments' --type ParamArrayFixture
  reject_case paramarray-expanded '#using "paramarray-fixture.dll"
int main() { return ParamArrayFixture::Numbers(1, 2, 3); }' 'no matching function' --type ParamArrayFixture
  log "PASS test-paramarrays: omitted/fresh arrays, explicit/null arguments, constructor and virtual dispatch"
}
step_test-constructors() {
  log "Managed allocation resolves constructors in their source access context"
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local args=(--type System.GC --type System.ArgumentOutOfRangeException --type System.IDisposable
    --type System.Runtime.InteropServices.CriticalHandle --type System.Runtime.ConstrainedExecution.CriticalFinalizerObject
    --type System.IntPtr --reference "$(cygpath -m "${refs[-1]}/System.Runtime.InteropServices.dll")")
  run_case constructor-access "$src/tests/constructor-access.cpp" $'17\n29\n41\n63\n23\n7' "${args[@]}"
  mkdir -p "$testdir/constructor-consumer"
  cat > "$testdir/constructor-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="constructor-access"><HintPath>../constructor-access.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/constructor-consumer/Program.cs" <<'EOF'
using System;
using System.Reflection;
using System.Runtime.InteropServices;
if (PrivateFactory.Create(42).Value != 42 || ProtectedFactory.Create(43).Read() != 43 ||
    typeof(PrivateFactory).GetConstructors().Length != 0 || typeof(ProtectedFactory).GetConstructors().Length != 0 ||
    !typeof(PrivateFactory).GetConstructor(BindingFlags.Instance | BindingFlags.NonPublic, null, new[] { typeof(int) }, null).IsPrivate ||
    !typeof(ProtectedFactory).GetConstructor(BindingFlags.Instance | BindingFlags.NonPublic, null, new[] { typeof(int) }, null).IsFamily)
    throw new Exception("factory constructor access or visibility lost");
var text = LiteralFactory.Create();
if (text.Read() != "λ factory" || text.Next.Read() != "accessor literal" || LiteralFactory.Default.Read() != "static literal" ||
    ConstructorProbes.Range() is not ArgumentOutOfRangeException range || range.ParamName != "faceIndex")
    throw new Exception("literal constructor overload or parameter name lost");
if (ConstructorProbes.Upcast() is not InterfaceFactory value || value is not IFactoryChild || value is not IDisposable ||
    typeof(InterfaceFactory).BaseType != typeof(object) || ((IFactoryValue)value).Read() != 42)
    throw new Exception("interface-only class Object identity or interface dispatch lost");
using var handle = (CriticalHandle)ConstructorProbes.Handle();
if (!handle.IsInvalid || typeof(ImportedFactory).BaseType != typeof(CriticalHandle))
    throw new Exception("imported base chain Object conversion lost");
Console.WriteLine("CLR factory access, literals and Object upcasts OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/constructor-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/constructor-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case constructor-private 'public ref class C { C(int) {} }; int main() { C^ c = gcnew C(1); return 0; }' 'private constructor'
  reject_case constructor-protected 'public ref class C { protected: C(int) {} }; int main() { C^ c = gcnew C(1); return 0; }' 'protected constructor'
  reject_case constructor-abstract 'public ref class C abstract { public: C() {} }; int main() { C^ c = gcnew C(); return 0; }' 'cannot allocate an abstract'
  reject_case constructor-native-new 'public ref class C {}; int main() { C^ c = new C(); return 0; }' 'unsupported AST expression: CXXNewExpr'
  log "PASS test-constructors: private/protected factories, nested/default allocations, conversions and access rejection"
}
step_test-objects() {
  log "Managed constructors, fields, inheritance, handles and member calls"
  run_case objects "$src/tests/objects.cpp" $'8\n16\nTrue\nTrue\nTrue\nTrue\nFalse\ntyped literal\n1\n2'
  run_case evaluation "$src/tests/evaluation.cpp" $'receiver\n3\nreceiver\n3\nreceiver\n5\n5'
  cat > "$testdir/default-gcnew.cpp" <<'EOF'
using namespace System;
public ref class DefaultBox { public: int Value = 23; };
int main() { DefaultBox^ box = gcnew DefaultBox; Console::WriteLine(box->Value); return 0; }
EOF
  run_case default-gcnew "$testdir/default-gcnew.cpp" '23'
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
  reject_case bad-constructor 'using namespace System; public ref class Box { public: Box(int x) {} }; int main() { Box^ b = gcnew Box("bad"); return 0; }' 'no matching constructor'
  reject_case bad-downcast 'using namespace System; public ref class Base {}; public ref class Derived : Base {}; int main() { Base^ b = gcnew Base(); Derived^ d = b; return 0; }' 'cannot initialize'
  reject_case private-field 'using namespace System; public ref class Box { int hidden; }; int main() { Box^ b = gcnew Box(); b->hidden = 1; return 0; }' 'private member'
  reject_case native-managed-pointer 'using namespace System; public ref class Box {}; int main() { Box* b = gcnew Box(); return 0; }' "native '\*' pointers"
  reject_case native-value-pointer-gc 'public value struct Point { System::String^ Text; }; int main() { Point* native = nullptr; return 0; }' 'native value pointers require proven GC-free element storage'
  reject_case missing-default-constructor 'public ref class B { public: B(int value) {} }; public ref class D : B {}; int main() { D^ d = gcnew D(); return 0; }' 'deleted'
  reject_case missing-imported-constructor 'using namespace System; int main() { Console^ c = gcnew Console(); return 0; }' 'no public metadata constructor'
  log "PASS test-objects: managed identity and Clang constructor/member checking"
}
step_test-adapter-gate() {
  log "Scope lookup must distinguish a shadowed managed type name from native XOR"
  run_case scope-xor "$src/tests/scope-xor.cpp" '4'
}
step_test-references() {
  log "Included-header #using, macro paths, friend identity and CLR access checks"
  mkdir -p "$testdir/friend-source" "$testdir/includes/nested"
  /usr/bin/python3 - "$here/keys" "$testdir/friend-source" <<'PY'
from pathlib import Path
import sys
key = bytes.fromhex(next(Path(sys.argv[1]).glob('MSShared-*.pubkey.hex')).read_text())
dest = Path(sys.argv[2])
(dest/'key.snk').write_bytes(key)
(dest/'Api.cs').write_text('''using System.Runtime.CompilerServices;
[assembly: InternalsVisibleTo("FriendBridge, PublicKey=''' + key.hex() + '''")]
namespace FriendApi {
internal class SecretApi { internal static int Read() => 40; }
public class PublicApi { internal static int Extra() => 2; private static int Hidden() => 7; protected static int Protected() => 9; }
}
''')
PY
  cat > "$testdir/friend-source/FriendLibrary.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><EnableNETAnalyzers>false</EnableNETAnalyzers><SignAssembly>true</SignAssembly><PublicSign>true</PublicSign><AssemblyOriginatorKeyFile>key.snk</AssemblyOriginatorKeyFile></PropertyGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/friend-source/FriendLibrary.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  cp "$testdir/friend-source/bin/Debug/net8.0/FriendLibrary.dll" "$testdir/"
  cat > "$testdir/includes/nested/reference.hpp" <<'EOF'
#pragma once
#if 0
#using "must-not-exist.dll" as_friend
#endif
/* #using "comment.dll" */
// #using "comment2.dll"
#us\
ing FRIEND_DLL as_friend
#warning header-reference-source-manager-check
EOF
  cat > "$testdir/includes/outer.hpp" <<'EOF'
#pragma once
#include "nested/reference.hpp"
#include "nested/reference.hpp"
EOF
  cat > "$testdir/friend.cpp" <<'EOF'
#include "includes/outer.hpp"
using namespace System;
int main() { Console::WriteLine(FriendApi::SecretApi::Read() + FriendApi::PublicApi::Extra()); return 0; }
EOF
  local friend_dll key
  friend_dll=$(cygpath -m "$testdir/FriendLibrary.dll")
  key=$(cygpath -m "$testdir/friend-source/key.snk")
  run_case friend-bridge "$testdir/friend.cpp" '42' --assembly-name FriendBridge --public-key "$key" \
    --type FriendApi.SecretApi --type FriendApi.PublicApi -D "FRIEND_DLL=\"$friend_dll\""
  /usr/bin/python3 - "$testdir/friend-bridge.dll.work/references.json" <<'PY'
import json, sys
refs = json.load(open(sys.argv[1], encoding='utf-8'))
assert len(refs) == 1 and refs[0]['as_friend'] and refs[0]['source'].replace('\\', '/').endswith('/includes/nested/reference.hpp'), refs
PY
  cat > "$testdir/friend-public-only.cpp" <<'EOF'
#using "FriendLibrary.dll" as_friend
int main() { FriendApi::PublicApi^ value=gcnew FriendApi::PublicApi(); System::Console::WriteLine("public as_friend metadata works without an internal grant"); return 0; }
EOF
  run_case friend-public-only "$testdir/friend-public-only.cpp" 'public as_friend metadata works without an internal grant' \
    --type FriendApi.PublicApi --public-key "$key"
  cat > "$testdir/friend-runtime-denied.cpp" <<'EOF'
#using "FriendLibrary.dll" as_friend
public ref class UngrantedFriend abstract sealed { public: static int Read() { return FriendApi::PublicApi::Extra(); } };
EOF
  compile "$(cygpath -m "$testdir/friend-runtime-denied.cpp")" --target library --type FriendApi.PublicApi \
    --public-key "$key" -o "$(cygpath -m "$testdir/friend-runtime-denied.dll")"
  compile "$(cygpath -m "$testdir/friend-runtime-denied.cpp")" --target library --type FriendApi.PublicApi \
    --assembly-name FriendBridge -o "$(cygpath -m "$testdir/friend-wrong-key.dll")"
  mkdir -p "$testdir/friend-runtime-consumer"
  cat > "$testdir/friend-runtime-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup></Project>
EOF
  cat > "$testdir/friend-runtime-consumer/Program.cs" <<'EOF'
using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
foreach(string file in args) {
    var context=new AssemblyLoadContext(file,true);
    context.Resolving+=(scope,name)=>scope.LoadFromAssemblyPath(Path.Combine(Path.GetDirectoryName(Path.GetFullPath(file)),name.Name+".dll"));
    var assembly=context.LoadFromAssemblyPath(Path.GetFullPath(file));
    try { assembly.GetType("UngrantedFriend").GetMethod("Read").Invoke(null,null); throw new Exception("runtime accepted ungranted internal access"); }
    catch(TargetInvocationException error) when(error.InnerException is MethodAccessException) { Console.WriteLine("runtime rejects ungranted internal access: "+Path.GetFileName(file)); }
    context.Unload();
}
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/friend-runtime-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/friend-runtime-consumer/bin/Debug/net8.0/consumer.dll")" \
    "$(cygpath -m "$testdir/friend-runtime-denied.dll")" "$(cygpath -m "$testdir/friend-wrong-key.dll")"
  reject_case internal-without-friend '#using "FriendLibrary.dll"
int main() { return 0; }' 'imported type is internal' --type FriendApi.SecretApi
  reject_case friend-private '#using "FriendLibrary.dll" as_friend
int main() { return FriendApi::PublicApi::Hidden(); }' 'no member named' --assembly-name FriendBridge --public-key "$key" --type FriendApi.PublicApi
  reject_case friend-protected '#using "FriendLibrary.dll" as_friend
int main() { return FriendApi::PublicApi::Protected(); }' 'protected member' --assembly-name FriendBridge --public-key "$key" --type FriendApi.PublicApi
  reject_case using-suffix '#using "FriendLibrary.dll" nonsense
int main() { return 0; }' 'unexpected tokens after #using'
  log "PASS test-references: included directives and signed friend calls run on CoreCLR"
}
step_test-properties() {
  run_case properties "$src/tests/properties.cpp" $'set\nget\n4\nreceiver\nget\nset\nget\n6\n7\nproperty finally\n2'
}
step_test-accessor-visibility() {
  compile "$(cygpath -m "$src/tests/accessor-visibility.cpp")" --target library -o "$(cygpath -m "$testdir/accessor-visibility.dll")"
  mkdir -p "$testdir/accessor-visibility-consumer"
  cp "$src/tests/AccessorVisibilityConsumer.cs" "$testdir/accessor-visibility-consumer/Program.cs"
  cat > "$testdir/accessor-visibility-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="accessor-visibility"><HintPath>../accessor-visibility.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/accessor-visibility-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/accessor-visibility-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case accessor-internal-import '#using "accessor-visibility.dll"
int main() { AccessorBox^ value=gcnew AccessorBox(); value->Value=9; return 0; }' 'no setter' --type AccessorBox
}
step_test-multicast() {
  local args=(--type System.Delegate --type System.MulticastDelegate --type System.Action)
  compile "$(cygpath -m "$src/tests/multicast.cpp")" --target library -o "$(cygpath -m "$testdir/multicast.dll")" "${args[@]}"
  mkdir -p "$testdir/multicast-consumer"
  cp "$src/tests/MulticastConsumer.cs" "$testdir/multicast-consumer/Program.cs"
  cat > "$testdir/multicast-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="multicast"><HintPath>../multicast.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/multicast-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/multicast-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case multicast-wrong-type 'public delegate void A(int value);
public delegate void B(int value);
int main() { A^ a=nullptr; B^ b=nullptr; a+=b; return 0; }' 'incompatible pointer types' "${args[@]}"
  reject_case multicast-const 'public delegate void A(int value);
int main() { A^ const a=nullptr; A^ b=nullptr; a+=b; return 0; }' 'const-qualified' "${args[@]}"
  reject_case multicast-rvalue 'public delegate void A(int value);
A^ Get() { return nullptr; }
int main() { A^ a=nullptr; Get()+=a; return 0; }' 'not assignable' "${args[@]}"
  reject_case multicast-object 'int main() { System::Object^ a=nullptr; System::Object^ b=nullptr; a+=b; return 0; }' 'invalid operands' "${args[@]}"
}
step_test-primitive-array-apis() {
  compile "$(cygpath -m "$src/tests/primitive-array-apis.cpp")" --target library \
    --type System.Int32 --type System.Int64 --type System.Double --type System.Type --type System.RuntimeTypeHandle \
    --type System.Enum --type System.Globalization.CultureInfo --type System.IFormatProvider \
    --type System.Boolean --type System.Byte --type System.SByte --type System.Int16 --type System.UInt16 \
    --type System.UInt32 --type System.UInt64 --type System.Char --type System.Single \
    --type System.Array --type System.Collections.IEnumerator \
    -o "$(cygpath -m "$testdir/primitive-array-apis.dll")"
  mkdir -p "$testdir/primitive-array-apis-consumer"
  cp "$src/tests/PrimitiveArrayApisConsumer.cs" "$testdir/primitive-array-apis-consumer/Program.cs"
  cat > "$testdir/primitive-array-apis-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="primitive-array-apis"><HintPath>../primitive-array-apis.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/primitive-array-apis-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/primitive-array-apis-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case boxing-ambiguous-null 'public ref class Slots { public: int Pick(System::Object^ value) { return 1; } int Pick(System::Type^ value) { return 2; } }; int main() { Slots^ s=gcnew Slots(); return s->Pick(0); }' 'ambiguous' --type System.Type
  reject_case array-native-pointer-value 'int main() { array<int>^ values=gcnew array<int>(1); int value=42; values->SetValue(&value,0); return 0; }' 'no matching member function' --type System.Array
  reject_case percent-native-value 'int main() { int value=42; %value; return 0; }' 'unary % requires a managed value-class operand'
}
step_test-printing-accessors() {
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  mkdir -p "$testdir/accessor-contract" "$testdir/printing-accessors-consumer"
  cat > "$testdir/accessor-contract/contract.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><AssemblyName>AccessorContract</AssemblyName><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>
EOF
  cat > "$testdir/accessor-contract/Contract.cs" <<'EOF'
namespace AccessorContract;
public class BaseTable {
    readonly int[] values=new int[2];
    [System.Runtime.CompilerServices.IndexerName("Lookup")]
    public virtual int this[int index] { get=>values[index]; set=>values[index]=value; }
    public string Title { get; private set; }="original title";
}
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/accessor-contract/contract.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  cp "$testdir/accessor-contract/bin/Debug/net8.0/AccessorContract.dll" "$testdir/"
  cp "$src/tests/printing-accessors.cpp" "$testdir/printing-accessors.cpp"
  /clang64/bin/clang++.exe -shared -O2 "$src/tests/semantic-literals-native.cpp" -o "$testdir/printing-accessors-native.dll"
  local args=(--type System.Resources.ResourceManager --type System.Reflection.Assembly --type System.Type --type System.ArgumentOutOfRangeException \
    --type System.Collections.Hashtable --type AccessorContract.BaseTable --type System.Delegate --type System.MulticastDelegate \
    --reference "$(cygpath -m "${refs[-1]}/System.Collections.NonGeneric.dll")" \
    --reference "$(cygpath -m "$testdir/AccessorContract.dll")")
  compile "$(cygpath -m "$testdir/printing-accessors.cpp")" --target library "${args[@]}" \
    --native-library printing-accessors-native.dll -o "$(cygpath -m "$testdir/printing-accessors.dll")"
  cp "$src/tests/PrintingAccessorsConsumer.cs" "$testdir/printing-accessors-consumer/Program.cs"
  cat > "$testdir/printing-accessors-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="printing-accessors"><HintPath>../printing-accessors.dll</HintPath></Reference><Reference Include="AccessorContract"><HintPath>../AccessorContract.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/printing-accessors-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  cp "$testdir/printing-accessors-native.dll" "$testdir/printing-accessors-consumer/bin/Debug/net8.0/"
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/printing-accessors-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case accessor-private-setter '#using "AccessorContract.dll"
public ref class Child : AccessorContract::BaseTable { public: void Write() { AccessorContract::BaseTable::Title::set("bad"); } }; int main() { return 0; }' 'no selected accessor' "${args[@]}"
  reject_case initializer-private-constructor 'public ref class Base { private: Base(System::String^ text) {} }; public ref class Child : Base { public: Child():Base("private") {} }; int main() { return 0; }' 'private constructor'
  reject_case inherited-private-setter 'public ref class Root { public: property int Value { int get() { return 0; } private: void set(int value) {} } }; public ref class Child : Root { public: void Write() { Child::Value::set(1); } }; int main() { return 0; }' 'private member'
}
step_test-nested-imports() {
  mkdir -p "$testdir/nested-contract" "$testdir/nested-peer" "$testdir/nested-imports-consumer"
  cat > "$testdir/nested-contract/contract.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><AssemblyName>NestedContract</AssemblyName><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>
EOF
  cat > "$testdir/nested-contract/Contract.cs" <<'EOF'
using System.Runtime.CompilerServices;
[assembly:InternalsVisibleTo("nested-friend")]
namespace NestedContract;
public class Outer {
    public enum Flags : uint { High=0x80000000, Low=1 }
    public const Flags Default=Flags.High;
    public static Flags Echo(Flags value)=>value;
    public static Flags Facility(int value)=>(Flags)((value>>16)&0x1fff);
    public class Node {
        public string Name { get; }
        public Flags Code;
        public Node(string text) { Name=text; }
        public class Detail { public string Name; public Detail(string text) { Name=text; } }
    }
    public struct Point { public int X; }
    internal class Hidden { public int Value; public Hidden(int value) { Value=value; } }
    private class Private {}
}
public class GenericOuter<T> { public class Nested {} }
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/nested-contract/contract.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  cp "$testdir/nested-contract/bin/Debug/net8.0/NestedContract.dll" "$testdir/"
  cat > "$testdir/nested-peer/peer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><AssemblyName>NestedPeer</AssemblyName><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="NestedContract"><HintPath>../NestedContract.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/nested-peer/Peer.cs" <<'EOF'
namespace NestedPeer;
public class Api {
    public static NestedContract.Outer.Flags Echo(NestedContract.Outer.Flags value)=>value;
    public static NestedContract.Outer.Node Node(NestedContract.Outer.Node value)=>value;
    public static NestedContract.Outer.Point Point(NestedContract.Outer.Point value) { value.X++; return value; }
}
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/nested-peer/peer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  cp "$testdir/nested-peer/bin/Debug/net8.0/NestedPeer.dll" "$testdir/"
  cp "$src/tests/nested-imports.cpp" "$testdir/nested-imports.cpp"
  local args=(--reference "$(cygpath -m "$testdir/NestedContract.dll")" --reference "$(cygpath -m "$testdir/NestedPeer.dll")" \
    --type 'NestedContract.Outer+Flags' --type 'NestedContract.Outer+Node' --type 'NestedContract.Outer+Point' \
    --type 'NestedContract.Outer+Node+Detail' --type NestedPeer.Api)
  compile "$(cygpath -m "$testdir/nested-imports.cpp")" --target library "${args[@]}" -o "$(cygpath -m "$testdir/nested-imports.dll")"
  cp "$src/tests/NestedImportsConsumer.cs" "$testdir/nested-imports-consumer/Program.cs"
  cat > "$testdir/nested-imports-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="nested-imports"><HintPath>../nested-imports.dll</HintPath></Reference><Reference Include="NestedContract"><HintPath>../NestedContract.dll</HintPath></Reference><Reference Include="NestedPeer"><HintPath>../NestedPeer.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/nested-imports-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/nested-imports-consumer/bin/Debug/net8.0/consumer.dll")"
  cat > "$testdir/nested-friend.cpp" <<'EOF'
#using "NestedContract.dll" as_friend
int main() { NestedContract::Outer::Hidden^ value=gcnew NestedContract::Outer::Hidden(32); System::Console::WriteLine(value->Value); return 0; }
EOF
  run_case nested-friend "$testdir/nested-friend.cpp" '32' --type 'NestedContract.Outer+Hidden'
  reject_case nested-not-friend '#using "NestedContract.dll"
int main() { return 0; }' 'imported type is internal' --type 'NestedContract.Outer+Hidden'
  reject_case nested-private '#using "NestedContract.dll" as_friend
int main() { return 0; }' 'unsupported private/protected nested import' --type 'NestedContract.Outer+Private'
  reject_case nested-generic '#using "NestedContract.dll"
int main() { return 0; }' 'unsupported nested generic import' --type 'NestedContract.GenericOuter`1+Nested'
}
step_test-semantic-literals() {
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  /clang64/bin/clang++.exe -std=c++17 -O2 -shared "$src/tests/semantic-literals-native.cpp" -o "$testdir/semantic-literals-native.dll"
  compile "$(cygpath -m "$src/tests/semantic-literals.cpp")" --target library \
    --type System.Globalization.CultureInfo --type System.Collections.Hashtable --type System.Exception \
    --reference "$(cygpath -m "${refs[-1]}/System.Collections.NonGeneric.dll")" \
    --native-library semantic-literals-native.dll \
    -o "$(cygpath -m "$testdir/semantic-literals.dll")"
  mkdir -p "$testdir/semantic-literals-consumer"
  cp "$src/tests/SemanticLiteralsConsumer.cs" "$testdir/semantic-literals-consumer/Program.cs"
  cat > "$testdir/semantic-literals-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="semantic-literals"><HintPath>../semantic-literals.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/semantic-literals-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  cp "$testdir/semantic-literals-native.dll" "$testdir/semantic-literals-consumer/bin/Debug/net8.0/"
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/semantic-literals-consumer/bin/Debug/net8.0/consumer.dll")"
}
step_test-argument-files() {
  /clang64/bin/python.exe - "$(cygpath -m "$testdir")" "$(cygpath -m "$CPPCLI_SDK")" "$(cygpath -m "$CPPCLI_ILASM")" "$(cygpath -m "$CPPCLI_CLANG")" <<'PY'
import json,sys
from pathlib import Path
work,sdk,ilasm,clang=sys.argv[1:]
work=Path(work)
source=work/'JSON arguments λ $.cpp'
source.write_text('int main() { System::Console::WriteLine(ARG_TEXT); return 0; }\n',encoding='utf-8')
arguments=[str(source),'-o',str(work/'argument-files.dll'),'--sdk',sdk,'--ilasm',ilasm,'--clang',clang,
           '-D','ARG_TEXT="JSON: λ $(literal) & quoted \\"text\\""']
(work/'argument-files.json').write_text(json.dumps(arguments,ensure_ascii=False),encoding='utf-8')
(work/'argument-files-invalid.json').write_text('[42]',encoding='utf-8')
PY
  /clang64/bin/python.exe "$build/cli-clang" --arguments-json "$(cygpath -m "$testdir/argument-files.json")"
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/argument-files.dll")" > "$testdir/argument-files.stdout"
  tr -d '\r' < "$testdir/argument-files.stdout" > "$testdir/argument-files.actual"
  printf '%s\n' 'JSON: λ $(literal) & quoted "text"' > "$testdir/argument-files.expected"
  cmp "$testdir/argument-files.expected" "$testdir/argument-files.actual"
  if /clang64/bin/python.exe "$build/cli-clang" --arguments-json "$(cygpath -m "$testdir/argument-files-invalid.json")" > "$testdir/argument-files-invalid.log" 2>&1; then
    die "accepted non-string JSON arguments"
  fi
  grep -q 'array of strings' "$testdir/argument-files-invalid.log"
  log "PASS JSON argument vectors: Unicode/spaced paths, macro quotes/metacharacters, nested metadata transport and invalid vector rejection"
}
step_test-conversions() {
  compile "$(cygpath -m "$src/tests/conversions.cpp")" --target library -o "$(cygpath -m "$testdir/conversions.dll")"
  mkdir -p "$testdir/conversions-consumer"
  cp "$src/tests/ConversionsConsumer.cs" "$testdir/conversions-consumer/Program.cs"
  cat > "$testdir/conversions-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="conversions"><HintPath>../conversions.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/conversions-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/conversions-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case conversion-unrelated 'public ref class C { public: static operator int(double value) { return (int)value; } }; int main() { return 0; }' 'containing type'
  reject_case conversion-arity 'public ref class C { public: static operator int(C^ value,int other) { return other; } }; int main() { return 0; }' 'one parameter'
  reject_case conversion-void 'public ref class C { public: static operator void(C^ value) {} }; int main() { return 0; }' 'non-void'
}
step_test-combined-access() {
  compile "$(cygpath -m "$src/tests/combined-access.cpp")" --target library -o "$(cygpath -m "$testdir/combined-access.dll")"
  cp "$src/tests/imported-combined-access.cpp" "$testdir/imported-combined-access.cpp"
  compile "$(cygpath -m "$testdir/imported-combined-access.cpp")" --target library --type CombinedAccess \
    -o "$(cygpath -m "$testdir/imported-combined-access.dll")"
  mkdir -p "$testdir/combined-access-consumer"
  cp "$src/tests/CombinedAccessConsumer.cs" "$testdir/combined-access-consumer/Program.cs"
  cat > "$testdir/combined-access-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="combined-access"><HintPath>../combined-access.dll</HintPath></Reference><Reference Include="imported-combined-access"><HintPath>../imported-combined-access.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/combined-access-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/combined-access-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case combined-local-and 'public ref class C { protected private: int Read() { return 42; } }; int main() { C^ c=gcnew C(); return c->Read(); }' 'protected member'
  reject_case combined-foreign-or '#using "combined-access.dll"
int main() { CombinedAccess^ c=gcnew CombinedAccess(); return c->OrMethod(); }' 'protected member' --type CombinedAccess
  reject_case combined-foreign-and '#using "combined-access.dll"
public ref class Foreign : CombinedAccess { public: int Read() { return AndMethod(); } }; int main() { return 0; }' 'undeclared identifier' --type CombinedAccess
}
step_test-string-enum-properties() {
  compile "$(cygpath -m "$src/tests/string-enum-properties.cpp")" --target library --type System.GC \
    -o "$(cygpath -m "$testdir/string-enum-properties.dll")"
  mkdir -p "$testdir/string-enum-properties-consumer"
  cp "$src/tests/StringEnumPropertiesConsumer.cs" "$testdir/string-enum-properties-consumer/Program.cs"
  cat > "$testdir/string-enum-properties-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="string-enum-properties"><HintPath>../string-enum-properties.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/string-enum-properties-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/string-enum-properties-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case string-native-buffer-comparison 'int main() { System::String^ value="text"; const char* buffer="text"; return value==buffer; }' 'comparison of distinct pointer types'
}
step_test-tracking-temporaries() {
  compile "$(cygpath -m "$src/tests/tracking-temporaries.cpp")" --target library --type System.GC \
    --type System.ArgumentException --type System.Delegate --type System.MulticastDelegate \
    -o "$(cygpath -m "$testdir/tracking-temporaries.dll")"
  mkdir -p "$testdir/tracking-temporaries-consumer"
  cp "$src/tests/TrackingTemporariesConsumer.cs" "$testdir/tracking-temporaries-consumer/Program.cs"
  cat > "$testdir/tracking-temporaries-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="tracking-temporaries"><HintPath>../tracking-temporaries.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/tracking-temporaries-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/tracking-temporaries-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case tracking-temporary-native 'public ref class Proxy { public: Proxy(int value) {} }; public ref class Calls { public: static void Read(Proxy& value) {} }; int main() { Calls::Read(Proxy(1)); return 0; }' 'cannot bind to a temporary'
  reject_case tracking-temporary-disposal 'public ref class Proxy { public: Proxy(int value) {} ~Proxy() {} }; public ref class Calls { public: static void Read(Proxy% value) {} }; int main() { Calls::Read(Proxy(1)); return 0; }' 'require full-expression cleanup'
  reject_case tracking-temporary-mixed-overloads 'public ref class Proxy { public: Proxy(int value) {} }; public ref class Calls { public: static void Read(Proxy% value) {} static void Read(int value) {} }; int main() { Calls::Read(Proxy(1)); return 0; }' 'no matching function'
}
step_test-implicit-boxing() {
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  compile "$(cygpath -m "$src/tests/implicit-boxing.cpp")" --target library --type System.Collections.Hashtable --type System.Collections.SortedList --type System.GC \
    --reference "$(cygpath -m "${refs[-1]}/System.Collections.NonGeneric.dll")" -o "$(cygpath -m "$testdir/implicit-boxing.dll")"
  mkdir -p "$testdir/implicit-boxing-consumer"
  cp "$src/tests/ImplicitBoxingConsumer.cs" "$testdir/implicit-boxing-consumer/Program.cs"
  cat > "$testdir/implicit-boxing-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="implicit-boxing"><HintPath>../implicit-boxing.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/implicit-boxing-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/implicit-boxing-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case boxing-native-pointer 'public ref class Slot { public: void Set(System::Object^ value) {} }; int main() { Slot^ s=gcnew Slot(); void* p=nullptr; s->Set(p); return 0; }' 'cannot initialize'
  reject_case boxing-private-setter 'public ref class Slot { public: property System::Object^ Value { System::Object^ get() { return nullptr; } private: void set(System::Object^ value) {} } }; int main() { Slot^ s=gcnew Slot(); s->Value=42; return 0; }' 'private member'
}
step_test-source-owned-types() {
  mkdir -p "$testdir/source-contract" "$testdir/source-peer" "$testdir/source-owned-consumer"
  cat > "$testdir/source-contract/contract.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><AssemblyName>SourceContract</AssemblyName><AssemblyVersion>1.0.0.0</AssemblyVersion><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>
EOF
  cat > "$testdir/source-contract/Contract.cs" <<'EOF'
namespace SourceContract;
public class Value { public virtual int Read() { throw new System.Exception("compile-only contract executed"); } }
EOF
  cat > "$testdir/source-peer/peer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><AssemblyName>SourcePeer</AssemblyName><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="SourceContract"><HintPath>../source-contract/bin/Debug/net8.0/SourceContract.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/source-peer/Peer.cs" <<'EOF'
namespace Peer;
public class Holder {
    readonly SourceContract.Value value;
    public Holder(SourceContract.Value value) { this.value=value; }
    public int Read() { return value.Read(); }
    public SourceContract.Value Current => value;
    public static SourceContract.Value[] EchoArray(SourceContract.Value[] values) => values;
    public static void Replace(ref SourceContract.Value value,SourceContract.Value replacement) { value=replacement; }
}
EOF
  for project in "$testdir/source-contract/contract.csproj" "$testdir/source-peer/peer.csproj"; do
    "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$project")" -nologo -v:q --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  done
  cp "$testdir/source-peer/bin/Debug/net8.0/SourcePeer.dll" "$testdir/SourcePeer.dll"
  cp "$src/tests/source-owned-types.cpp" "$testdir/source-owned-types.cpp"
  compile "$(cygpath -m "$testdir/source-owned-types.cpp")" --target library --type Peer.Holder \
    --source-type SourceContract.Value -o "$(cygpath -m "$testdir/SourceContract.dll")"
  cp "$src/tests/SourceOwnedConsumer.cs" "$testdir/source-owned-consumer/Program.cs"
  cat > "$testdir/source-owned-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="SourceContract"><HintPath>../SourceContract.dll</HintPath></Reference><Reference Include="SourcePeer"><HintPath>../SourcePeer.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/source-owned-consumer/consumer.csproj")" -nologo -v:q --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/source-owned-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case source-type-undefined 'int main() { return 0; }' 'requires a real definition' --source-type Missing.Value
  reject_case source-type-wrong-kind 'namespace Missing { public value struct Value { int x; }; } int main() { return 0; }' 'requires a real definition' --source-type Missing.Value
  reject_case source-type-wrong-assembly '#using "SourcePeer.dll"
namespace SourceContract { public ref class Value {}; } int main() { return 0; }' 'references another assembly' --source-type SourceContract.Value --type Peer.Holder
}
step_test-events() {
  local args=(--type System.Delegate --type System.MulticastDelegate --type System.Action)
  compile "$(cygpath -m "$src/tests/events.cpp")" --target library -o "$(cygpath -m "$testdir/events.dll")" "${args[@]}"
  cp "$src/tests/imported-events.cpp" "$testdir/imported-events.cpp"
  compile "$(cygpath -m "$testdir/imported-events.cpp")" --target library -o "$(cygpath -m "$testdir/imported-events.dll")" \
    --type EventSource --type Changed "${args[@]}"
  mkdir -p "$testdir/events-consumer"
  cp "$src/tests/EventsConsumer.cs" "$testdir/events-consumer/Program.cs"
  cat > "$testdir/events-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="events"><HintPath>../events.dll</HintPath></Reference><Reference Include="imported-events"><HintPath>../imported-events.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/events-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/events-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case event-read 'public delegate void D(int value);
public ref class E { public: event D^ Changed; };
int main() { E^ e=gcnew E(); D^ d=e->Changed; return 0; }' 'events can only' "${args[@]}"
  reject_case event-assign 'public delegate void D(int value);
public ref class E { public: event D^ Changed; };
int main() { E^ e=gcnew E(); e->Changed=nullptr; return 0; }' 'events can only' "${args[@]}"
  reject_case event-raise-private 'public delegate void D(int value);
public ref class E { public: event D^ Changed; };
int main() { E^ e=gcnew E(); e->Changed(42); return 0; }' 'private member' "${args[@]}"
  reject_case event-wrong-handler 'public delegate void D(int value);
public delegate void Other(int value);
public ref class E { public: event D^ Changed; };
int main() { E^ e=gcnew E(); Other^ d=nullptr; e->Changed+=d; return 0; }' 'cannot initialize' "${args[@]}"
  reject_case event-bad-accessor 'public delegate void D(int value);
public ref class E { public: event D^ Changed { void add(int value) {} void remove(D^ value) {} } };
int main() { return 0; }' 'event accessor delegate type mismatch' "${args[@]}"
  reject_case event-imported-protected '#using "events.dll"
int main() { EventSource^ e=gcnew EventSource(); e->Custom(42); return 0; }' 'protected member' --type EventSource --type Changed "${args[@]}"
  reject_case event-imported-internal '#using "events.dll"
int main() { EventSource^ e=gcnew EventSource(); Changed^ d=nullptr; e->InternalEvent+=d; return 0; }' 'no member named' --type EventSource --type Changed "${args[@]}"
  reject_case event-bad-raise 'public delegate void D(int value);
public ref class E { public: event D^ Changed { void add(D^ value) {} void remove(D^ value) {} void raise(double value) {} } };
int main() { return 0; }' 'event raise signature mismatch' "${args[@]}"
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
PropertyBase property = new PropertyDerived();
if (VirtualProperties.Read(property) != 17) throw new Exception("virtual property getter did not dispatch");
VirtualProperties.Write(property, 20);
if (VirtualProperties.Read(property) != 31 || !typeof(PropertyBase).GetProperty("Value").GetMethod.IsVirtual ||
    VirtualProperties.Read(new PropertyOverride()) != 99)
    throw new Exception("virtual property setter/metadata/C# override lost");
Console.WriteLine("dispatch metadata OK");
class Override : Counter {
    public Override(int value) : base(value) {}
    public override int Read() => 42;
}
class PropertyOverride : PropertyBase {
    public override int Value { get => 99; set { } }
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
  reject_case destructor 'public value class B { public: ~B() {} }; int main() { return 0; }' 'destructors currently require source ref classes'
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
step_test-native-arrays() {
  /clang64/bin/clang++.exe -std=c++17 -shared -O2 "$src/tests/native-arrays-native.cpp" -o "$testdir/cppcli-native-arrays.dll"
  run_case native-arrays "$src/tests/native-arrays.cpp" 'native array allocation, zeroing, pointer/POD stride, address/store/updates, GC and delete[] OK' \
    --native-library cppcli-native-arrays.dll --type System.GC --type System.OverflowException
  reject_case native-array-ctor 'struct Native { Native() {} int Value; }; int main() { Native* values = new Native[2]; return 0; }' 'trivial constructor'
}
step_test-compound() {
  run_case compound "$src/tests/compound.cpp" 'compound flags, promotion/narrowing, signedness, floats and single RHS/receiver/index/pointer evaluation OK'
  reject_case compound-native-enum 'enum class Flags { Low=1, High=2 }; int main() { Flags flags=Flags::Low; flags |= Flags::High; return 0; }' 'invalid operands'
  reject_case compound-mixed-enum 'public enum class First { Low=1 }; public enum class Second { High=2 }; int main() { First flags=First::Low; flags |= Second::High; return 0; }' 'invalid operands'
  reject_case compound-const-enum 'public enum class Flags { Low=1, High=2 }; int main() { const Flags flags=Flags::Low; flags |= Flags::High; return 0; }' 'invalid operands'
  reject_case compound-void-pointer 'int main() { void* pointer=nullptr; pointer+=1; return 0; }' 'arithmetic'
}
step_test-pointer-arithmetic() {
  /clang64/bin/clang++.exe -std=c++17 -shared -O2 "$src/tests/native-pointer-arithmetic-native.cpp" -o "$testdir/cppcli-pointer-arithmetic.dll"
  run_case native-pointer-arithmetic "$src/tests/native-pointer-arithmetic.cpp" 'native pointer strides, signed offsets/differences, ordering, updates and single evaluation OK' \
    --native-library cppcli-pointer-arithmetic.dll --type System.GC
  compile "$(cygpath -m "$src/tests/value-pointer-arithmetic.cpp")" --target library --type System.GC \
    --type System.Runtime.InteropServices.StructLayoutAttribute --type System.Runtime.InteropServices.LayoutKind \
    --type System.Runtime.InteropServices.FieldOffsetAttribute -o "$(cygpath -m "$testdir/value-pointer-arithmetic.dll")"
  mkdir -p "$testdir/value-pointer-arithmetic-consumer"
  cp "$src/tests/ValuePointerArithmeticConsumer.cs" "$testdir/value-pointer-arithmetic-consumer/Program.cs"
  cat > "$testdir/value-pointer-arithmetic-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><AllowUnsafeBlocks>true</AllowUnsafeBlocks><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="value-pointer-arithmetic"><HintPath>../value-pointer-arithmetic.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/value-pointer-arithmetic-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/value-pointer-arithmetic-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case pointer-incomplete 'struct Incomplete; int main() { Incomplete* value=nullptr; value+=1; return 0; }' 'incomplete type'
  reject_case pointer-function 'int main() { void (*function)()=nullptr; function+=1; return 0; }' 'arithmetic'
  reject_case reference-cast-managed 'public ref class Owner { public: int Value; }; int Read(const int& value) { return value; } int main() { Owner^ owner=gcnew Owner(); return Read((const int&)owner->Value); }' 'native reference cannot bind to movable managed storage'
}
step_test-native-pins() {
  run_case pin-native "$src/tests/pin-native.cpp" 'native pointer pin initialization, const parameters, managed/native reassignment and GC OK' --type System.GC
}
step_test-native-strings() {
  run_case native-strings "$src/tests/native-strings.cpp" $'λ😀\n😀\nOK\nsource-metadata native UTF-16/narrow String constructors, slices, null, copy ownership and GC OK' --type System.GC
}
step_test-function-pointers() {
  /clang64/bin/clang++.exe -std=c++17 -shared -O2 "$src/tests/function-pointers-native.cpp" -o "$testdir/cppcli-function-pointers.dll"
  run_case function-pointers "$src/tests/function-pointers.cpp" 'native function pointer storage, returns, single callee evaluation, mixed ABI, references and outputs OK' \
    --native-library cppcli-function-pointers.dll --type System.GC
  reject_case function-pointer-variadic 'int main() { int (*pointer)(int,...)=nullptr; return pointer(1,2); }' 'fixed non-variadic signature'
  reject_case function-pointer-bool 'int main() { bool (*pointer)()=nullptr; return pointer(); }' 'native function pointer signatures require scalars'
  reject_case function-pointer-managed 'using namespace System; int main() { int (*pointer)(Object^)=nullptr; return pointer(nullptr); }' 'native function pointer signatures require scalars'
  run_case native-sizeof "$src/tests/native-sizeof.cpp" 'native sizeof/alignof, SDK layout and unevaluated operands OK'
  reject_case sizeof-managed 'public value struct Value { int Field; }; int main() { return sizeof(Value); }' 'managed records require CLR layout support'
}
step_test-native-allocation() {
  /clang64/bin/clang++.exe -std=c++17 -shared -O2 "$src/tests/native-allocation-native.cpp" -o "$testdir/cppcli-native-allocation.dll"
  run_case native-allocation "$src/tests/native-allocation.cpp" 'native scalar/POD allocation, UCRT ownership transfer, erased/null deletion, GC and initializer exceptions OK' \
    --native-library cppcli-native-allocation.dll --type System.GC --type System.InvalidOperationException
  reject_case native-new-array-init 'int main() { int* values = new int[2]{1,2}; delete[] values; return 0; }' 'only empty value initialization'
  reject_case native-new-nontrivial 'struct Native { Native() {} int Value; }; int main() { Native* value = new Native(); return 0; }' 'cannot execute a nontrivial native constructor'
  reject_case native-new-custom '#include <cstddef>
struct Native { static void* operator new(std::size_t); int Value; }; int main() { Native* value = new Native(); return 0; }' 'custom native allocation operators'
  reject_case native-new-nested-side-effect 'int Init() { return 42; } int Consume(int value, int* pointer) { delete pointer; return value; } int main() { return Consume(1, new int(Init())); }' 'side-effectful initializer requires an empty evaluation stack'
}
step_test-native-com() {
  /clang64/bin/clang++.exe -std=c++17 -shared -O2 "$src/tests/com-native.cpp" "$src/tests/native-com-extended.cpp" -luuid -o "$testdir/cppcli-native-com.dll"
  run_case native-com "$src/tests/native-com.cpp" 'native COM vtable dispatch, inheritance, GUID reference, HRESULT, outputs and receiver evaluation OK' \
    --native-library cppcli-native-com.dll --type System.Guid
  reject_case native-com-bool '#include <windows.h>
#include <unknwn.h>
struct IFlag : IUnknown { virtual bool Flag() = 0; }; int main() { IFlag* value = nullptr; return value->Flag(); }' 'native COM return values require a scalar'
  reject_case native-com-byvalue '#include <windows.h>
#include <unknwn.h>
struct POD { int Value; }; struct IArg : IUnknown { virtual int Call(POD value) = 0; }; int main() { IArg* receiver = nullptr; POD value = {1}; return receiver->Call(value); }' 'native COM signatures require scalars'
}
step_test-sdk-constants() {
  run_case sdk-constants "$src/tests/sdk-constants.cpp" $'3\n9223372036854775813\n9'
  reject_case sdk-dynamic-operator '#include <windows.h>
#include <dwrite.h>
int Dynamic(DWRITE_FONT_SIMULATIONS value) { return value | DWRITE_FONT_SIMULATIONS_BOLD; }
int main() { return Dynamic(DWRITE_FONT_SIMULATIONS_NONE); }' 'without an emitted definition'
  reject_case sdk-side-effect '#include "sdk-constants.hpp"
int main() { int value = 0; return SdkIntegerConstant(++value); }' 'without an emitted definition' -I "$(cygpath -m "$src/tests")"
}
step_test-init-locals() {
  run_case init-locals "$src/tests/init-locals.cpp" $'0\n6'
}
step_test-native-inline-arrays() {
  /clang64/bin/clang++.exe -std=c++17 -shared -O2 "$src/tests/native-inline-arrays-native.cpp" -o "$testdir/cppcli-inline-arrays.dll"
  run_case native-inline-arrays "$src/tests/native-inline-arrays.cpp" 'native inline arrays: packed offsets, scalar/pointer/POD strides, volatile updates, decay, copy, single evaluation and GC OK' \
    --native-library cppcli-inline-arrays.dll --type System.GC
}
step_test-assembly-attributes() {
  reject_case assembly-friend-nonliteral '[assembly: System::Runtime::CompilerServices::InternalsVisibleTo(42)]; int main() { return 0; }' 'InternalsVisibleTo requires one literal assembly identity'
  reject_case assembly-forward-native 'struct Native { int Value; }; [assembly: System::Runtime::CompilerServices::TypeForwardedTo(Native::typeid)]; int main() { return 0; }' 'TypeForwardedTo currently requires an imported'
  reject_case assembly-attribute-qualifier '[assembly: Custom::InternalsVisibleTo("Peer")]; int main() { return 0; }' 'unsupported qualified assembly attribute name'
}
step_test-assembly-versions() {
  compile "$(cygpath -m "$src/tests/assembly-versions.cpp")" --target library \
    --type System.Reflection.AssemblyFileVersionAttribute --type System.Reflection.AssemblyInformationalVersionAttribute \
    --type System.Reflection.AssemblyTitleAttribute -o "$(cygpath -m "$testdir/assembly-versions.dll")"
  mkdir -p "$testdir/assembly-versions-consumer"
  cat > "$testdir/assembly-versions-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="assembly-versions"><HintPath>../assembly-versions.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/assembly-versions-consumer/Program.cs" <<'EOF'
using System;
using System.Reflection;
var assembly=typeof(VersionedAssembly).Assembly;
if(assembly.GetName().Version!=new Version(1,2,3,4) ||
   assembly.GetCustomAttribute<AssemblyFileVersionAttribute>()?.Version!="7.8.9.10" ||
   assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion!="0.3+source λ" ||
   assembly.GetCustomAttribute<AssemblyTitleAttribute>()?.Title!="Source-only C++/CLI" || VersionedAssembly.Answer()!=42)
    throw new Exception("incorrect source assembly identity/string attributes");
Console.WriteLine("source assembly version, file/informational/title metadata and UTF-8 serialization OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/assembly-versions-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/assembly-versions-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case assembly-version-bad '[assembly:System::Reflection::AssemblyVersion("1.2.*.4")]; int main() { return 0; }' 'AssemblyVersion requires four numeric components'
  reject_case assembly-version-range '[assembly:System::Reflection::AssemblyVersion("1.2.65535.4")]; int main() { return 0; }' 'AssemblyVersion requires four numeric components'
  reject_case assembly-version-duplicate '[assembly:System::Reflection::AssemblyVersion("1.2.3.4")]; [assembly:System::Reflection::AssemblyVersion("1.2.3.4")]; int main() { return 0; }' 'duplicate assembly metadata attribute'
}
step_test-native-bitfields() {
  /clang64/bin/clang++.exe -std=c++17 -shared -O2 "$src/tests/native-bitfields-native.cpp" -o "$testdir/cppcli-native-bitfields.dll"
  run_case native-bitfields "$src/tests/native-bitfields.cpp" 'native bitfields: SDK layout, signed/cross-byte masks, narrow results, neighbor preservation, updates, volatile byte units and GC OK' \
    --native-library cppcli-native-bitfields.dll --type System.GC
  reject_case native-bitfield-wide-volatile 'struct Native { volatile unsigned Value:3; }; int main() { Native value; return value.Value; }' 'volatile native bitfields currently require byte-sized allocation units'
}
step_test-native-fields() {
  /clang64/bin/clang++.exe -std=c++17 -fms-extensions -shared -O2 -DCPPCLI_NATIVE_BUILD "$src/tests/native-fields-native.cpp" -o "$testdir/cppcli-native-fields.dll"
  run_case native-fields "$src/tests/native-fields.cpp" 'native POD aggregate, packed nested fields, addresses and single evaluation OK' --native-library cppcli-native-fields.dll
  reject_case native-bitfield-reference 'struct Native { unsigned Value:3; }; unsigned Read(const unsigned& value) { return value; } int main() { Native value; return Read(value.Value); }' 'native bitfields have no independently addressable native storage'
  reject_case native-array-field-init 'struct Native { int Values[2]; }; int main() { Native value = {{1, 2}}; return 0; }' 'native POD whole-array field operations'
}

step_test-contextual-delegate() {
  run_case contextual-delegate "$src/tests/contextual-delegate.cpp" \
    'contextual delegate declarations, WPF-style parameter/field identifiers and delegate invocation PASS' \
    --type System.MulticastDelegate --type System.Delegate
}

step_test-native-spooler() {
  /clang64/bin/clang++.exe -std=c++17 -O2 -DSPOOLER_NATIVE_ORACLE \
    "$src/tests/native-spooler.cpp" -lwinspool -o "$testdir/native-spooler-oracle.exe"
  compile "$(cygpath -m "$src/tests/native-spooler.cpp")" \
    --native-import "$(cygpath -m /clang64/lib/libwinspool.a)" \
    -o "$(cygpath -m "$testdir/native-spooler.dll")"
  "$testdir/native-spooler-oracle.exe" > "$testdir/native-spooler.expected"
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/native-spooler.dll")" > "$testdir/native-spooler.actual"
  cmp "$testdir/native-spooler.expected" "$testdir/native-spooler.actual" || die "spooler native/CLI enumeration mismatch"
  /usr/bin/python3 - "$testdir/native-spooler.dll.work/native-imports.json" \
    "$testdir/native-spooler.dll.work/assembly.il" <<'PY'
from pathlib import Path
import json,sys
manifest=json.loads(Path(sys.argv[1]).read_text())
assert manifest['imports']['EnumPrintersW']['library'].upper()=='WINSPOOL.DRV'
assert 'WINSPOOL.DRV' in Path(sys.argv[2]).read_text().upper()
PY
  reject_case spooler-unbound '#include <windows.h>
#include <winspool.h>
int main() { DWORD bytes=0,count=0; return EnumPrintersW(PRINTER_ENUM_LOCAL,nullptr,1,nullptr,0,&bytes,&count); }' 'without an emitted definition'
  reject_case spooler-user-declaration 'extern "C" int EnumPrintersW();
int main() { return EnumPrintersW(); }' 'without an emitted definition' \
    --native-import "$(cygpath -m /clang64/lib/libwinspool.a)"
  log "PASS native-spooler: trusted WINSPOOL.DRV import and real read-only EnumPrintersW match native SDK oracle"
}
step_test-windows-api() {
  log "MinGW import archives -> lazy Windows API P/Invoke bindings"
  compile "$(cygpath -m "$src/tests/windows-api.cpp")" --target library \
    --native-import "$(cygpath -m /clang64/lib/libadvapi32.a)" \
    --native-import "$(cygpath -m /clang64/lib/libkernel32.a)" \
    -o "$(cygpath -m "$testdir/windows-api.dll")"
  mkdir -p "$testdir/windows-api-consumer"
  cp "$src/tests/WindowsApiConsumer.cs" "$testdir/windows-api-consumer/Program.cs"
  cat > "$testdir/windows-api-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="windows-api"><HintPath>../windows-api.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/windows-api-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/windows-api-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case windows-api-unbound '#include <windows.h>
int main() { return RegCloseKey(NULL); }' 'without an emitted definition'
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
step_test-layout() {
  log "CLR layout metadata and field offsets"
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local layout_args=(--type System.Runtime.InteropServices.LayoutKind --type System.Runtime.InteropServices.CharSet
    --reference "$(cygpath -m "${refs[-1]}/System.Runtime.InteropServices.dll")")
  compile "$(cygpath -m "$src/tests/layout.cpp")" --target library "${layout_args[@]}" -o "$(cygpath -m "$testdir/layout.dll")"
  mkdir -p "$testdir/layout-consumer"
  cat > "$testdir/layout-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="layout"><HintPath>../layout.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/layout-consumer/Program.cs" <<'EOF'
using System;
using System.Runtime.InteropServices;
if (!typeof(LayoutUnion).IsExplicitLayout || Marshal.SizeOf<LayoutUnion>() != 16 || Marshal.OffsetOf<LayoutUnion>("Tail").ToInt32() != 8)
    throw new Exception("explicit value layout lost");
if (LayoutChecks.UnionValue(0x3f800000) != 1.0f) throw new Exception("overlapping fields lost");
if (!typeof(PackedLayout).IsLayoutSequential || Marshal.SizeOf<PackedLayout>() != 12 || Marshal.OffsetOf<PackedLayout>("Value").ToInt32() != 1 ||
    typeof(PackedLayout).StructLayoutAttribute.Pack != 1 || typeof(PackedLayout).StructLayoutAttribute.CharSet != CharSet.Unicode)
    throw new Exception("sequential pack, size or charset lost");
var obj = new LayoutObject { Value = 21, Tail = -2 };
if (!typeof(LayoutObject).IsExplicitLayout || Marshal.OffsetOf<LayoutObject>("Tail").ToInt32() != 8 || obj.Doubled != 42 || obj.Tail != -2)
    throw new Exception("explicit reference layout lost");
if (!typeof(LayoutObject).GetField("Secret", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).IsAssembly)
    throw new Exception("internal field visibility lost through layout attribute");
Console.WriteLine("CLR layout metadata OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/layout-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/layout-consumer/bin/Debug/net8.0/consumer.dll")"
  local prefix='using namespace System::Runtime::InteropServices; '
  reject_case layout-missing-offset "$prefix"'[StructLayout(LayoutKind::Explicit)] public value struct Bad { int Value; }; int main() { return 0; }' 'requires FieldOffset' "${layout_args[@]}"
  reject_case layout-negative-offset "$prefix"'[StructLayout(LayoutKind::Explicit)] public value struct Bad { [FieldOffset(-1)] int Value; }; int main() { return 0; }' 'nonnegative Int32 offset' "${layout_args[@]}"
  reject_case layout-sequential-offset "$prefix"'[StructLayout(LayoutKind::Sequential)] public value struct Bad { [FieldOffset(0)] int Value; }; int main() { return 0; }' 'FieldOffset requires explicit layout' "${layout_args[@]}"
  reject_case layout-static-offset "$prefix"'[StructLayout(LayoutKind::Explicit)] public value struct Bad { [FieldOffset(0)] static int Value; }; int main() { return 0; }' 'requires a managed instance field' "${layout_args[@]}"
  reject_case layout-pack "$prefix"'[StructLayout(LayoutKind::Sequential, Pack=3)] public value struct Bad { int Value; }; int main() { return 0; }' 'Pack must be zero' "${layout_args[@]}"
  reject_case layout-unknown-constant "$prefix"'[StructLayout(LayoutKind::Explicit)] public value struct Bad { [FieldOffset(Missing)] int Value; }; int main() { return 0; }' 'undeclared identifier' "${layout_args[@]}"
  reject_case layout-runtime-offset "$prefix"'int Position() { return 0; } [StructLayout(LayoutKind::Explicit)] public value struct Bad { [FieldOffset(Position())] int Value; }; int main() { return 0; }' 'constant expression' "${layout_args[@]}"
  reject_case layout-unknown-kind "$prefix"'[StructLayout(LayoutKind::Missing)] public value struct Bad { int Value; }; int main() { return 0; }' 'Missing' "${layout_args[@]}"
  reject_case layout-negative-size "$prefix"'[StructLayout(LayoutKind::Sequential, Size=-1)] public value struct Bad { int Value; }; int main() { return 0; }' 'Size must be a nonnegative' "${layout_args[@]}"
  log "PASS test-layout: explicit/sequential CLR layout, overlapping fields, pack, size and charset"
}
step_test-value-pointers() {
  log "Native managed-value pointers distinct from boxed handles"
  /clang64/bin/clang++.exe -std=c++17 -shared -O2 "$src/tests/value-pointers-native.cpp" -o "$testdir/cppcli-value-pointers.dll"
  compile "$(cygpath -m "$src/tests/value-pointers.cpp")" --target library --type System.GC \
    --native-library cppcli-value-pointers.dll -o "$(cygpath -m "$testdir/value-pointers.dll")"
  mkdir -p "$testdir/value-pointers-consumer"
  cp "$src/tests/ValuePointersConsumer.cs" "$testdir/value-pointers-consumer/Program.cs"
  cat > "$testdir/value-pointers-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><AllowUnsafeBlocks>true</AllowUnsafeBlocks></PropertyGroup><ItemGroup><Reference Include="value-pointers"><HintPath>../value-pointers.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/value-pointers-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  cp "$testdir/cppcli-value-pointers.dll" "$testdir/value-pointers-consumer/bin/Debug/net8.0/"
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/value-pointers-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case value-pointer-box-mix 'public value struct V { int X; }; void Bad(V^ box) { V* pointer = box; } int main() { return 0; }' 'native value pointers and boxed value handles cannot be implicitly mixed'
  reject_case value-pointer-to-box 'public value struct V { int X; }; void Bad(V* pointer) { V^ box = pointer; } int main() { return 0; }' 'native value pointers and boxed value handles cannot be implicitly mixed'
  reject_case value-pointer-gc 'public value struct V { System::String^ Text; }; void Bad(V* pointer) {} int main() { return 0; }' 'native value pointers require proven GC-free element storage'
  reject_case value-pointer-heap-address 'public value struct V { int X; }; public ref class C { public: V Value; }; void Bad(C^ owner) { V* pointer = &owner->Value; } int main() { return 0; }' 'native reference cannot bind to movable managed storage'
  reject_case value-pointer-managed-cast 'public value struct V { int X; }; void Bad(V* pointer) { System::Object^ box = safe_cast<System::Object^>(pointer); } int main() { return 0; }' 'native value pointers cannot be used as managed cast operands'
  reject_case value-pointer-delete 'public value struct V { int X; }; void Bad(V* pointer) { delete pointer; } int main() { return 0; }' 'native deletion and delete'
  reject_case value-pointer-argument 'public value struct V { int X; }; void Native(V* value) {} void Bad(V^ box) { Native(box); } int main() { return 0; }' 'native value pointers and boxed value handles cannot be implicitly mixed'
  reject_case value-pointer-return 'public value struct V { int X; }; V* Bad(V^ box) { return box; } int main() { return 0; }' 'native value pointers and boxed value handles cannot be implicitly mixed'
  reject_case value-pointer-conditional 'public value struct V { int X; }; void Bad(V^ box, V* pointer, bool choose) { V^ result = choose ? box : pointer; } int main() { return 0; }' 'native value pointers and boxed value handles cannot be implicitly mixed'
  reject_case value-pointer-redeclaration 'public value struct V { int X; }; void Bad(V^ box); void Bad(V* pointer) {} int main() { return 0; }' 'native-pointer and boxed-handle signatures cannot share one canonical Clang declaration'
  reject_case value-pointer-array 'public value struct V { int X; }; using namespace System; void Bad(array<V*>^ values) {} int main() { return 0; }' 'native value-pointer array/generic elements are not supported yet'
  reject_case value-pointer-property 'public value struct V { int X; }; public ref class Holder { public: property V* Value { V* get() { return nullptr; } } }; int main() { return 0; }' 'native value-pointer properties are not supported yet'
  log "PASS test-value-pointers: native value signatures/storage, C# pinned arrays and boxed-handle rejection"
}
step_test-finalizers() {
  /clang64/bin/clang++.exe -std=c++17 -O2 -shared "$src/tests/finalizers-native.cpp" -o "$testdir/finalizers-native.dll"
  compile "$(cygpath -m "$src/tests/finalizers.cpp")" --target library --native-library finalizers-native.dll \
    --type System.IDisposable --type System.InvalidOperationException -o "$(cygpath -m "$testdir/finalizers.dll")"
  mkdir -p "$testdir/finalizers-consumer"
  cp "$src/tests/FinalizersConsumer.cs" "$testdir/finalizers-consumer/Program.cs"
  cat > "$testdir/finalizers-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="finalizers"><HintPath>../finalizers.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/finalizers-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  cp "$testdir/finalizers-native.dll" "$testdir/finalizers-consumer/bin/Debug/net8.0/"
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/finalizers-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case finalizer-parameters 'public ref class C { !C(int value) {} }; int main() { return 0; }' 'finalizer takes no parameters'
  reject_case finalizer-missing 'public ref class C { !C(); }; int main() { return 0; }' 'finalizer requires an explicit source body'
  reject_case finalizer-conflict 'public ref class C { !C() {} void Finalize() {} }; int main() { return 0; }' 'finalizer conflicts with an explicit Dispose/Finalize'
  reject_case finalizer-value 'public value struct C { !C() {} }; int main() { return 0; }' 'finalizers require'
  reject_case finalizer-private 'public ref class C { public: C() {} private: !C() {} }; int main() { C^ value=gcnew C(); value->!C(); return 0; }' 'private member'
}

step_test-destructors() {
  log "Managed destructor dispose pattern, base cleanup and nested CLR identity"
  compile "$(cygpath -m "$src/tests/destructors.cpp")" --target library --type System.InvalidOperationException \
    --type System.IO.MemoryStream --type System.IO.Stream --type System.IDisposable -o "$(cygpath -m "$testdir/destructors.dll")"
  mkdir -p "$testdir/destructors-consumer"
  cp "$src/tests/DestructorsConsumer.cs" "$testdir/destructors-consumer/Program.cs"
  cat > "$testdir/destructors-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="destructors"><HintPath>../destructors.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/destructors-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/destructors-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case destructor-conflict 'public ref class B { public: ~B() {} void Dispose() {} }; int main() { return 0; }' 'destructor conflicts with an explicit Dispose/Finalize'
  reject_case destructor-defaulted 'public ref class B { public: ~B() = default; }; int main() { return 0; }' 'destructor requires an explicit source body'
  reject_case delete-native-incomplete 'struct Native; int main() { Native* value = nullptr; delete value; return 0; }' 'native deletion requires scalar or trivial native POD storage'
  log "PASS test-destructors: IDisposable dispatch, exception-safe base cleanup and genuine nested types"
}
step_test-native-storage() {
  log "Native references and Clang-sized GC-free POD storage"
  mkdir -p "$testdir/native-storage-fields"
  cat > "$testdir/native-storage-fields/Api.cs" <<'EOF'
namespace FieldsApi {
public struct WithHiddenValue { private long value; public WithHiddenValue(long value) { this.value = value; } public long Read() => value; }
public struct WithHiddenReference { private object value; public WithHiddenReference(object value) { this.value = value; } public object Read() => value; }
}
EOF
  cat > "$testdir/native-storage-fields/FieldsApi.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/native-storage-fields/FieldsApi.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  local fields=$testdir/native-storage-fields/bin/Debug/net8.0/FieldsApi.dll
  /clang64/bin/clang++.exe -std=c++17 -shared -O2 "$src/tests/native-storage-native.cpp" -o "$testdir/cppcli-native-storage.dll"
  compile "$(cygpath -m "$src/tests/native-storage.cpp")" --target library --native-library cppcli-native-storage.dll \
    --reference "$(cygpath -m "$fields")" --type FieldsApi.WithHiddenValue \
    --type System.Guid --type System.GC -o "$(cygpath -m "$testdir/native-storage.dll")"
  mkdir -p "$testdir/native-storage-consumer"
  cp "$src/tests/NativeStorageConsumer.cs" "$testdir/native-storage-consumer/Program.cs"
  cat > "$testdir/native-storage-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="native-storage"><HintPath>../native-storage.dll</HintPath></Reference><Reference Include="FieldsApi"><HintPath>../native-storage-fields/bin/Debug/net8.0/FieldsApi.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/native-storage-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  cp "$testdir/cppcli-native-storage.dll" "$testdir/native-storage-consumer/bin/Debug/net8.0/"
  cp "$fields" "$testdir/native-storage-consumer/bin/Debug/net8.0/"
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/native-storage-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case native-reference-array 'using namespace System; void Change(int& value) { value = 1; } void Bad(array<Int32>^ values) { Change(values[0]); } int main() { return 0; }' 'native reference cannot bind to movable managed storage'
  reject_case native-reference-alias-array 'using NativeIntRef = int&; using namespace System; void Change(NativeIntRef value) { value = 1; } void Bad(array<Int32>^ values) { Change(values[0]); } int main() { return 0; }' 'native reference cannot bind to movable managed storage'
  reject_case native-reference-field 'public ref class Owner { public: int Value; }; void Change(int& value) { value = 1; } void Bad(Owner^ value) { Change(value->Value); } int main() { return 0; }' 'native reference cannot bind to movable managed storage'
  reject_case native-reference-tracking 'void Change(int& value) { value = 1; } void Bad(int% value) { Change(value); } int main() { return 0; }' 'native reference cannot bind to movable managed storage'
  reject_case native-reference-gc-value 'public value class HasReference { public: System::String^ Text; }; void Bad(HasReference& value) {} int main() { return 0; }' 'native references require a primitive or proven GC-free value/native layout'
  reject_case native-reference-hidden-gc 'void Bad(FieldsApi::WithHiddenReference& value) {} int main() { return 0; }' 'native references require a primitive or proven GC-free value/native layout' --reference "$(cygpath -m "$fields")" --type FieldsApi.WithHiddenReference
  reject_case native-record-nontrivial 'struct Native { Native() {} int Value; }; Native Read() { return Native(); } int main() { return 0; }' 'native POD storage cannot execute a nontrivial native constructor'
  reject_case native-record-overaligned 'struct alignas(16) Native { int Value; }; Native Read() { Native value; return value; } int main() { return 0; }' 'native POD layout exceeds supported CLR size/alignment'
  log "PASS test-native-storage: native pointer reference metadata, stack/pinned bindings and real GUID byte layout"
}
step_test-pinning() {
  log "CLR pinned locals and moving-GC interior pointers across MinGW native calls"
  /clang64/bin/clang++.exe -std=c++17 -shared -O2 "$src/tests/pinning-native.cpp" -o "$testdir/cppcli-pinning.dll"
  compile "$(cygpath -m "$src/tests/pinning.cpp")" --target library --native-library cppcli-pinning.dll \
    --type System.GC --type System.InvalidOperationException -o "$(cygpath -m "$testdir/pinning.dll")"
  mkdir -p "$testdir/pinning-consumer"
  cat > "$testdir/pinning-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="pinning"><HintPath>../pinning.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/pinning-consumer/Program.cs" <<'EOF'
using System;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
var chars = "café 東京 😀".ToCharArray();
int collections = 0;
using var stop = new CancellationTokenSource();
var collector = Task.Run(() => {
    while (!stop.IsCancellationRequested) {
        for (int j = 0; j < 128; ++j) GC.KeepAlive(new byte[4096]);
        GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
        Interlocked.Increment(ref collections);
    }
});
try {
    for (int i = 0; i < 5; ++i) {
        var text = new string(chars); // No interned or permanently pinned literal.
        ushort[] data = { 65535, 0 };
        int before = Volatile.Read(ref collections);
        if (PinBridge.Read(text, data) != 42 || data[1] != 65530) throw new Exception("native pinning corrupted managed data");
        if (Volatile.Read(ref collections) <= before) throw new Exception("no compacting collection during native call");
        ushort[] assigned = { 65535, 0 };
        before = Volatile.Read(ref collections);
        if (PinBridge.AssignedRead(text, assigned) != 42 || assigned[1] != 65530)
            throw new Exception("default/assigned pin slots corrupted managed data");
        if (Volatile.Read(ref collections) <= before) throw new Exception("no compacting collection during assigned native call");
        ushort[] returned = { 65535, 0 };
        before = Volatile.Read(ref collections);
        if (PinBridge.ReturnedRead(text, returned) != 42 || returned[1] != 65530)
            throw new Exception("returned string interior pointer lost its managed root");
        if (Volatile.Read(ref collections) <= before) throw new Exception("no compacting collection during returned native call");
        ref readonly char first = ref PinBridge.StringChars(text);
        ref readonly char scoped = ref PinBridge.StringCharsInPinScope(text);
        int[] elements = { 1, 99 };
        ref int element = ref PinBridge.ArrayElement(elements, 1);
        var owner = new InteriorFieldOwner();
        ref int field = ref PinBridge.FieldElement(owner);
        GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
        if (first != 'c' || scoped != 'c' || element != 99 || field != 17)
            throw new Exception("returned managed byref did not survive collection");
        element = 42; field = 23;
        if (elements[1] != 42 || owner.Value != 23) throw new Exception("returned heap byref lost its original storage");
        byte[] word = { 0x11, 0x22, 0x33, 0x44 };
        before = Volatile.Read(ref collections);
        if (PinBridge.Cast(word) != 0x88776655u || word[0] != 0x55 || word[3] != 0x88)
            throw new Exception("pinned byte to native word cast failed");
        if (Volatile.Read(ref collections) <= before) throw new Exception("no compacting collection during cast native call");
        if (PinBridge.Early(text) != 'c' || PinBridge.Alias(new[] { 1, 99 }) != 99) throw new Exception("managed pointer read failed");
        try { PinBridge.Fail(text); throw new Exception("missing pinned-scope exception"); }
        catch (InvalidOperationException) { }
        if (PinBridge.Loop(new[] { 1, 2, 3 }) != 4) throw new Exception("pin scope loop control failed");
    }
} finally { stop.Cancel(); collector.GetAwaiter().GetResult(); }
var body = typeof(PinBridge).GetMethod("Read").GetMethodBody();
if (body.LocalVariables.Count(v => v.IsPinned) != 2 || !body.ExceptionHandlingClauses.Any(c => c.Flags == ExceptionHandlingClauseOptions.Finally))
    throw new Exception("missing pinned locals or scope cleanup");
Console.WriteLine("pinning and moving GC OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/pinning-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  cp "$testdir/cppcli-pinning.dll" "$testdir/pinning-consumer/bin/Debug/net8.0/"
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/pinning-consumer/bin/Debug/net8.0/consumer.dll")"
  step_test-native-pins
  reject_case pin-parameter '#include <vcclr.h>
int Read(pin_ptr<int> value) { return *value; } int main() { return 0; }' 'restricted to block-scoped locals'
  reject_case pin-for-scope '#include <vcclr.h>
int main() { int n = 1; for (pin_ptr<int> p = &n; n < 3; ++n) {} return 0; }' 'restricted to block-scoped locals'
  reject_case pointer-void-arithmetic 'int main() { void* value = nullptr; value++; return 0; }' 'arithmetic'
  reject_case interior-return '#include <vcclr.h>
interior_ptr<int> Escape() { int n = 1; return &n; } int main() { return 0; }' 'interior pointer return must refer directly to managed heap storage'
  reject_case interior-parameter-copy-return '#include <vcclr.h>
interior_ptr<int> Escape(int n) { return &n; } int main() { return 0; }' 'interior pointer return must refer directly to managed heap storage'
  reject_case interior-alias-return '#include <vcclr.h>
interior_ptr<int> Escape() { int n = 1; interior_ptr<int> alias = &n; return alias; } int main() { return 0; }' 'interior pointer return must refer directly to managed heap storage'
  reject_case interior-value-field-return '#include <vcclr.h>
public value class StackValue { public: int Field; }; interior_ptr<int> Escape() { StackValue value; return &value.Field; } int main() { return 0; }' 'interior pointer return must refer directly to managed heap storage'
  reject_case interior-conditional-return '#include <vcclr.h>
using namespace System; interior_ptr<int> Escape(array<Int32>^ values, bool choose) { int n = 1; return choose ? &values[0] : &n; } int main() { return 0; }' 'interior pointer return must refer directly to managed heap storage'
  reject_case pin-return '#include <vcclr.h>
pin_ptr<int> Escape() { int n = 1; return &n; } int main() { return 0; }' 'restricted to block-scoped locals'
  reject_case pin-implicit-reinterpret '#include <vcclr.h>
using namespace System; void Bad(array<Byte>^ bytes) { pin_ptr<Byte> data = &bytes[0]; unsigned int* word = data; } int main() { return 0; }' 'no viable conversion'
  reject_case pin-static-const '#include <vcclr.h>
using namespace System; void Bad(String^ text) { pin_ptr<const wchar_t> data = PtrToStringChars(text); char* raw = static_cast<char*>(data); } int main() { return 0; }' 'cannot discard element const'
  log "PASS test-pinning: Unicode strings and mutable arrays survive concurrent compacting GC in native calls"
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
step_test-switch() {
  log "Scoped CLI enum bitwise operators, Flags metadata and switch control flow"
  compile "$(cygpath -m "$src/tests/switch.cpp")" --target library --type System.InvalidOperationException \
    -o "$(cygpath -m "$testdir/switch.dll")"
  mkdir -p "$testdir/switch-consumer"
  cat > "$testdir/switch-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="switch"><HintPath>../switch.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/switch-consumer/Program.cs" <<'EOF'
using System;
using Example;
if (Switches.Combine(Bits.A, Bits.B) != Bits.Both || Switches.Mask(Bits.Both, Bits.A) != Bits.A || Switches.Toggle(Bits.Both, Bits.A) != Bits.B ||
    (byte)Switches.Invert(Bits.A) != 254 || Switches.InvertWide(Wide.High) != (Wide)0x7fffffffffffffffUL)
    throw new Exception("CLI enum operation type or width lost");
if (!typeof(Bits).IsDefined(typeof(FlagsAttribute), false) || ((Bits)3).ToString() != "Both" || ((Bits)255).ToString() != "255")
    throw new Exception("Flags enum metadata lost");
for (int i = 0; i < 4; i++) if (Switches.Convert((Bits)i) != i * 10) throw new Exception("enum case conversion failed");
try { Switches.Convert((Bits)4); throw new Exception("default exception lost"); } catch (InvalidOperationException) { }
if (Switches.FallThrough(1) != 30 || Switches.FallThrough(2) != 20 || Switches.FallThrough(3) != 90 || Switches.FallThrough(4) != 40 || Switches.FallThrough(5) != 40 ||
    Switches.Evaluations() != 5 || Switches.NoDefault(0) != 7 || Switches.NoDefault(5) != 9)
    throw new Exception("fallthrough/default/condition evaluation failed");
if (Switches.WideCases(Wide.High) != 1 || Switches.WideCases(Wide.Max) != 2 || Switches.WideCases((Wide)0) != 3 || Switches.NegativeCase(Negative.Low) != 4)
    throw new Exception("64-bit switch constants truncated");
if (Switches.Nested(9) != 102 || Switches.Nested(8) != 42) throw new Exception("nested switch/loop break or continue targets lost");
int cleanup = 0;
if (Switches.WithFinally(1, ref cleanup) != 42 || cleanup != 1 || Switches.WithFinally(2, ref cleanup) != 7 || cleanup != 3)
    throw new Exception("switch return/break skipped finally");
Console.WriteLine("CLI enum operators and switches OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/switch-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/switch-consumer/bin/Debug/net8.0/consumer.dll")"
  cat > "$testdir/switch-reference.cpp" <<'EOF'
#using "switch.dll"
using namespace System;
int main() { Example::Bits value = Example::Bits::A | Example::Bits::B; Console::WriteLine(Example::Switches::Convert(value)); return 0; }
EOF
  run_case switch-reference "$testdir/switch-reference.cpp" '30' --type Example.Bits --type Example.Switches
  reject_case enum-implicit-integer 'public enum class E { A }; int main() { int value = E::A; return value; }' 'cannot initialize'
  reject_case enum-mixed-bits 'public enum class A { X }; public enum class B { X }; int main() { auto value = A::X | B::X; return 0; }' 'invalid operands'
  reject_case native-scoped-bits 'enum class E { A, B }; int main() { auto value = E::A | E::B; return 0; }' 'invalid operands'
  reject_case switch-duplicate 'int main() { switch(1) { case 1: return 0; case 1: return 1; } return 0; }' 'duplicate case'
  log "PASS test-switch: typed enum operations and C# consumption of switch control flow"
}
step_test-generics() {
  log "Closed CLR generic classes, value types, interfaces and bound member signatures"
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local generic_args=(--reference "$(cygpath -m "${refs[-1]}/System.Collections.dll")"
    --type 'System.Collections.Generic.List`1' --type 'System.Collections.Generic.Dictionary`2'
    --type 'System.Collections.Generic.KeyValuePair`2' --type 'System.Collections.Generic.IComparer`1' --type 'System.Nullable`1'
    --type 'System.Collections.Generic.IEnumerable`1' --type 'System.Collections.Generic.IEnumerator`1'
    --type System.Collections.IEnumerable --type System.Collections.IEnumerator --type System.IDisposable)
  mkdir -p "$testdir/generic-fixture"
  cat > "$testdir/generic-fixture/fixture.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><AssemblyName>generic-fixture</AssemblyName><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup></Project>
EOF
  cat > "$testdir/generic-fixture/Fixture.cs" <<'EOF'
namespace GenericFixture {
    public interface ICombiner<TKey, TValue> { TValue Combine(TKey key, TValue value); }
    public class Slot<T> { public T Value; public static T Last; public Slot(T value) { Value = value; Last = value; } public T Echo(T value) => value; }
    public class Child<T> : Slot<T> { public Child(T value) : base(value) { } }
}
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/generic-fixture/fixture.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  cp "$testdir/generic-fixture/bin/Debug/net8.0/generic-fixture.dll" "$testdir/"
  compile "$(cygpath -m "$src/tests/generics.cpp")" --target library "${generic_args[@]}" \
    --reference "$(cygpath -m "$testdir/generic-fixture.dll")" --type 'GenericFixture.Slot`1' --type 'GenericFixture.Child`1' --type 'GenericFixture.ICombiner`2' \
    -o "$(cygpath -m "$testdir/generics.dll")"
  mkdir -p "$testdir/generic-consumer"
  cat > "$testdir/generic-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="generics"><HintPath>../generics.dll</HintPath></Reference><Reference Include="generic-fixture"><HintPath>../generic-fixture.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/generic-consumer/Program.cs" <<'EOF'
using System;
using System.Collections.Generic;
using GenericFixture;
if (GenericBridge.ListSum(38) != 42) throw new Exception("bound List<int> method signatures failed");
if (GenericBridge.Enumerate(40) != 42) throw new Exception("generic/nongeneric enumeration interface dispatch failed");
var dictionary = GenericBridge.DictionaryValue("café", 42);
int found = 0;
if (dictionary["café"] != 42 || !GenericBridge.Lookup(dictionary, "café", ref found) || found != 42 || GenericBridge.Lookup(dictionary, "missing", ref found))
    throw new Exception("generic dictionary/byref signatures failed");
var pair = GenericBridge.Pair("東京", 17);
if (pair.Key != "東京" || pair.Value != 17 || GenericBridge.Key(pair) != "東京" || GenericBridge.Value(pair) != 17 ||
    GenericBridge.Empty().HasValue || GenericBridge.NullableValue(42) != 42)
    throw new Exception("generic value type construction/property/default failed");
if (GenericBridge.Nested(42)[0][0] != 42) throw new Exception("nested generic type signatures failed");
IComparer<int> comparer = new IntComparer();
if (comparer.Compare(42, 1) != 41) throw new Exception("generic interface implementation failed");
ICombiner<string, int> combiner = new PairCombiner();
if (combiner.Combine("ab", 40) != 42) throw new Exception("two-argument generic base/member binding failed");
var slot = new Slot<int>(40);
if (GenericBridge.Field(slot) != 41 || slot.Value != 41 || GenericBridge.StaticField(99) != 99 || Slot<int>.Last != 99 || GenericBridge.Child(42).Value != 42)
    throw new Exception("generic field/static field/base constructor signatures failed");
if (GenericBridge.StaticField64(5000000002) != 5000000002 || Slot<long>.Last != 5000000002 || Slot<int>.Last != 42)
    throw new Exception("generic static fields lost type-instance identity");
if (typeof(GenericBridge).GetMethod("Pair").ReturnType != typeof(KeyValuePair<string, int>) ||
    typeof(GenericBridge).GetMethod("DictionaryValue").ReturnType != typeof(Dictionary<string, int>))
    throw new Exception("closed generic API type identity lost");
Console.WriteLine("closed CLR generic APIs OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/generic-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/generic-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case generic-missing-interface 'using namespace System::Collections::Generic; public ref class Missing : IComparer<int> {}; int main() { return 0; }' 'missing interface slot' "${generic_args[@]}"
  reject_case generic-allocate-interface 'using namespace System::Collections::Generic; int main() { IComparer<int>^ value = gcnew IComparer<int>(); return 0; }' 'cannot allocate a managed interface' "${generic_args[@]}"
  reject_case generic-native-pointer 'using namespace System::Collections::Generic; int main() { List<void*>^ value = gcnew List<void*>(); return 0; }' 'native pointers and typed boxed handles' "${generic_args[@]}"
  log "PASS test-generics: real CLR generic types, fields, constructors, methods and interface dispatch"
}
step_test-indexers() {
  log "Default/named indexed properties through Clang pseudo-object semantics"
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  compile "$(cygpath -m "$src/tests/indexers.cpp")" --target library \
    --reference "$(cygpath -m "${refs[-1]}/System.Collections.dll")" --type 'System.Collections.Generic.Dictionary`2' \
    -o "$(cygpath -m "$testdir/indexers.dll")"
  mkdir -p "$testdir/indexer-consumer"
  cat > "$testdir/indexer-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="indexers"><HintPath>../indexers.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/indexer-consumer/Program.cs" <<'EOF'
using System;
using System.Collections.Generic;
using System.Reflection;
var value = new Indexed();
value[1] = 37;
IndexerBridge.Update(value);
if (value.Reads != 1 || value.Writes != 2 || IndexerBridge.Receivers() != 1 || IndexerBridge.Indices() != 1)
    throw new Exception("indexed update evaluation order/count lost");
if (value[1] != 42 || IndexerBridge.Read(value) != 42 || IndexerBridge.Named(value) != 142 ||
    typeof(Indexed).GetCustomAttribute<DefaultMemberAttribute>().MemberName != "Item" || typeof(Indexed).GetProperty("Item").GetIndexParameters()[0].ParameterType != typeof(int))
    throw new Exception("indexer metadata or read binding failed");
var dictionary = new Dictionary<string,int> { ["café"] = 40 };
if (IndexerBridge.DictionaryItem(dictionary, "café") != 42 || dictionary["café"] != 42 || IndexerBridge.First("東京") != '東')
    throw new Exception("imported generic/string indexers failed");
try { IndexerBridge.Read(null); throw new Exception("null indexer receiver did not throw"); } catch (NullReferenceException) { }
var overloaded = new OverloadedIndex();
if (IndexerBridge.Overloaded(overloaded) != 42 || overloaded[1] != 40 || overloaded["name"] != 2 ||
    typeof(OverloadedIndex).GetProperties().Length != 2 || typeof(OverloadedIndex).GetProperty("Hidden") != null)
    throw new Exception("overloaded/out-of-line indexed properties or internal visibility failed");
Console.WriteLine("CLR indexed properties OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/indexer-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/indexer-consumer/bin/Debug/net8.0/consumer.dll")"
  cat > "$testdir/indexer-reference.cpp" <<'EOF'
#using "indexers.dll"
using namespace System;
int main() { Indexed^ value = gcnew Indexed(); value[1] = 42; Console::WriteLine(value[1]); OverloadedIndex^ other = gcnew OverloadedIndex(); other[1] = 40; other[__cli_literal("name")] = 2; Console::WriteLine(other[1] + other[__cli_literal("name")]); return 0; }
EOF
  run_case indexer-reference "$testdir/indexer-reference.cpp" $'42\n42' --type Indexed --type OverloadedIndex
  reject_case indexer-getter-type 'public ref class C { public: property int default[int] { double get(int value) { return 0; } } }; int main() { return 0; }' 'getter type does not match'
  reject_case indexer-index-type 'using namespace System; public ref class C { public: property int default[int] { int get(String^ value) { return 0; } } }; int main() { return 0; }' 'indexed getter parameter does not match'
  reject_case indexer-setter-type 'public ref class C { public: property int default[int] { void set(int index, double value) {} } }; int main() { return 0; }' 'setter type does not match'
  reject_case indexer-two-indices 'public ref class C { public: property int default[int,int] { int get(int x, int y) { return 0; } } }; int main() { return 0; }' 'require one index parameter'
  log "PASS test-indexers: source/imported CLR indexers and compound evaluation"
}
step_test-type-tokens() {
  log "Managed type tokens, closed generics and reliability accessor metadata"
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local args=(--type System.Type --type System.RuntimeTypeHandle --type 'System.Collections.Generic.List`1'
    --type System.Runtime.InteropServices.CriticalHandle --type System.IntPtr
    --type System.Runtime.ConstrainedExecution.Consistency --type System.Runtime.ConstrainedExecution.Cer
    --type System.Runtime.ConstrainedExecution.ReliabilityContractAttribute
    --reference "$(cygpath -m "${refs[-1]}/System.Collections.dll")" --reference "$(cygpath -m "${refs[-1]}/System.Runtime.InteropServices.dll")")
  compile "$(cygpath -m "$src/tests/type-tokens.cpp")" --target library "${args[@]}" -o "$(cygpath -m "$testdir/type-tokens.dll")"
  compile "$(cygpath -m "$src/tests/reliability.cpp")" --target library "${args[@]}" -o "$(cygpath -m "$testdir/reliability.dll")"
  mkdir -p "$testdir/type-token-consumer"
  cat > "$testdir/type-token-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="type-tokens"><HintPath>../type-tokens.dll</HintPath></Reference><Reference Include="reliability"><HintPath>../reliability.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/type-token-consumer/Program.cs" <<'EOF'
using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.ConstrainedExecution;
if (TypeTokens.Source() != typeof(TokenMarker) || TypeTokens.Value() != typeof(TokenValue) || TypeTokens.Mode() != typeof(TokenMode) ||
    TypeTokens.Text() != typeof(string) || TypeTokens.Integer() != typeof(int) || TypeTokens.Wide() != typeof(ulong) ||
    TypeTokens.Array() != typeof(int[]) || TypeTokens.Pointer() != typeof(float).MakePointerType() ||
    TypeTokens.Generic() != typeof(List<string>) || TypeTokens.Nested() != typeof(List<List<int>>))
    throw new Exception("managed type token identity mismatch");
var getter = typeof(ReliableHandle).GetProperty("IsInvalid").GetMethod;
var contract = getter.GetCustomAttribute<ReliabilityContractAttribute>();
if (contract.ConsistencyGuarantee != Consistency.WillNotCorruptState || contract.Cer != Cer.Success || !getter.IsVirtual ||
    !new ReliableHandle().IsInvalid || ReliableHandle.Read() != 42)
    throw new Exception("factory-shaped reliability accessor metadata/dispatch failed");
contract = typeof(ReliableHandle).GetMethod("Read").GetCustomAttribute<ReliabilityContractAttribute>();
if (contract.ConsistencyGuarantee != Consistency.MayCorruptInstance || contract.Cer != Cer.MayFail)
    throw new Exception("static reliability metadata lost");
Console.WriteLine("CLR managed type tokens and reliability metadata OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/type-token-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NoWarn=SYSLIB0004
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/type-token-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case type-token-expression 'int main() { int value = 0; value::typeid; return 0; }' 'template parameter' "${args[@]}"
  reject_case type-token-incomplete 'ref class Forward; int main() { Forward::typeid; return 0; }' 'incomplete managed typeid' "${args[@]}"
  reject_case type-token-native 'class Native {}; int main() { Native::typeid; return 0; }' 'native C++ class is not a managed type' "${args[@]}"
  reject_case reliability-unknown 'public ref class C { public: [ReliabilityContract(99,0)] static int F() { return 0; } }; int main() { return 0; }' 'ReliabilityContract' "${args[@]}"
  reject_case reliability-target '[ReliabilityContract(0,0)] public ref class C {}; int main() { return 0; }' 'unsupported declaration target' "${args[@]}"
  log "PASS test-type-tokens: CLR Type identity, generic/array/pointer operands and reliability attributes"
}
step_test-delegates() {
  log "CLR delegate metadata, method binding and native-to-managed invocation"
  local args=(--type System.Delegate --type System.MulticastDelegate --type System.InvalidOperationException --type System.Action)
  compile "$(cygpath -m "$src/tests/delegates.cpp")" --target library "${args[@]}" -o "$(cygpath -m "$testdir/delegates.dll")"
  mkdir -p "$testdir/delegate-consumer"
  /clang64/bin/clang++.exe -std=c++17 -O2 -shared "$src/tests/delegate-native.cpp" -o "$testdir/delegate-consumer/delegate-native.dll"
  cp "$src/tests/DelegateConsumer.cs" "$testdir/delegate-consumer/Program.cs"
  cat > "$testdir/delegate-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><AllowUnsafeBlocks>true</AllowUnsafeBlocks><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="delegates"><HintPath>../delegates.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/delegate-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  cp "$testdir/delegate-consumer/delegate-native.dll" "$testdir/delegate-consumer/bin/Debug/net8.0/"
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/delegate-consumer/bin/Debug/net8.0/consumer.dll")"
  cat > "$testdir/delegate-reference.cpp" <<'EOF'
#using "delegates.dll"
using namespace System;
int main() { Scalar^ value = DelegateBridge::Static(); Console::WriteLine(value(21)); return 0; }
EOF
  run_case delegate-reference "$testdir/delegate-reference.cpp" '42' --type Scalar --type DelegateBridge "${args[@]}"
  cat > "$testdir/imported-delegate.cpp" <<'EOF'
#using "delegates.dll"
using namespace System;
public ref class ImportedDelegateBindings {
    static int _count;
    static void Increment() { ++_count; }
public:
    static Action^ Create() { return gcnew Action(Increment); }
    static Scalar^ Double() { return gcnew Scalar(&ImportedDelegateBindings::Twice); }
    static int Twice(int value) { return value * 2; }
    static int Count() { return _count; }
};
int main() {
    Action^ action = ImportedDelegateBindings::Create(); action(); action->Invoke();
    Console::WriteLine(ImportedDelegateBindings::Count());
    Scalar^ value = ImportedDelegateBindings::Double(); Console::WriteLine(value(21));
    CounterTarget^ target = gcnew CounterTarget(7);
    Scalar^ instance = gcnew Scalar(target, &CounterTarget::Add); Console::WriteLine(instance(35));
    BaseTarget^ derived = gcnew DerivedTarget();
    Scalar^ dispatch = gcnew Scalar(derived, &BaseTarget::Apply); Console::WriteLine(dispatch(32));
    return 0;
}
EOF
  run_case imported-delegate "$testdir/imported-delegate.cpp" $'2\n42\n42\n42' --type Scalar --type CounterTarget --type BaseTarget --type DerivedTarget "${args[@]}"
  mkdir -p "$testdir/imported-delegate-consumer"
  cat > "$testdir/imported-delegate-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="imported-delegate"><HintPath>../imported-delegate.dll</HintPath></Reference><Reference Include="delegates"><HintPath>../delegates.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/imported-delegate-consumer/Program.cs" <<'EOF'
using System;
var action = ImportedDelegateBindings.Create();
if (action.GetType() != typeof(Action)) throw new Exception("imported delegate identity lost");
GC.Collect(); GC.WaitForPendingFinalizers(); action();
if (ImportedDelegateBindings.Count() != 1 || ImportedDelegateBindings.Double()(21) != 42)
    throw new Exception("imported delegate source binding or lifetime lost");
Console.WriteLine("Imported CLR delegates and Action binding OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/imported-delegate-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/imported-delegate-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case imported-delegate-signature 'using namespace System; int Wrong(int value) { return value; } int main() { Action^ action = gcnew Action(Wrong); return 0; }' 'no matching constructor' "${args[@]}"
  reject_case delegate-missing-method 'public delegate int D(int); public ref class C { public: static int F(int); }; int main() { D^ d = gcnew D(&C::F); return 0; }' 'delegate target has no emitted definition' "${args[@]}"
  log "PASS test-delegates: CLR construction, virtual slots, multicast, signatures, GC and native callbacks"
}
step_test-com() {
  log "COM import/GUID/interface metadata and native MinGW marshalling"
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local args=(--type System.IntPtr --type System.Type --type System.RuntimeTypeHandle --type System.Runtime.InteropServices.Marshal --type System.Runtime.InteropServices.ComInterfaceType --type System.Runtime.InteropServices.UnmanagedType
    --type System.Runtime.InteropServices.GuidAttribute --type System.Runtime.InteropServices.InterfaceTypeAttribute
    --type System.Runtime.InteropServices.ClassInterfaceType --type System.Runtime.InteropServices.ClassInterfaceAttribute --type System.Runtime.InteropServices.ComVisibleAttribute
    --reference "$(cygpath -m "${refs[-1]}/System.Runtime.InteropServices.dll")")
  compile "$(cygpath -m "$src/tests/com.cpp")" --target library "${args[@]}" -o "$(cygpath -m "$testdir/com.dll")"
  mkdir -p "$testdir/com-consumer"
  /clang64/bin/clang++.exe -std=c++17 -O2 -shared "$src/tests/com-native.cpp" -luuid -o "$testdir/com-consumer/com-probe-native.dll"
  cp "$src/tests/ComConsumer.cs" "$testdir/com-consumer/Program.cs"
  cat > "$testdir/com-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><AllowUnsafeBlocks>true</AllowUnsafeBlocks><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="com"><HintPath>../com.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/com-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  cp "$testdir/com-consumer/com-probe-native.dll" "$testdir/com-consumer/bin/Debug/net8.0/"
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/com-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case com-bad-guid '[Guid("invalid")] public interface class I {}; int main() { return 0; }' 'canonical GUID' "${args[@]}"
  reject_case com-no-guid '[ComImport] public interface class I {}; int main() { return 0; }' 'interface with a GuidAttribute' "${args[@]}"
  reject_case com-bad-mode '[InterfaceType(99)] public interface class I {}; int main() { return 0; }' 'invalid InterfaceType' "${args[@]}"
  reject_case com-bad-marshal 'public interface class I { void F([MarshalAs(99)] int x); }; int main() { return 0; }' 'unsupported MarshalAs' "${args[@]}"
  reject_case com-duplicate '[ComImport][ComImport][Guid("138EF579-12A0-4B01-8913-675542958CBD")] public interface class I {}; int main() { return 0; }' 'duplicate interop' "${args[@]}"
  reject_case com-member-target 'public ref class C { public: [In] int F() { return 0; } }; int main() { return 0; }' 'unsupported declaration target' "${args[@]}"
  reject_case com-boxed-nativeint 'using namespace System; public ref class C { public: static void F(IntPtr^ value) {} }; int main() { return 0; }' 'boxed native integers' "${args[@]}"
  log "PASS test-com: actual COM interface dispatch, native BOOL/uint64 and HRESULT preservation/translation"
}
step_test-value-field-order() {
  run_case value-field-order "$src/tests/value-field-order.cpp" \
    'imported SafeHandle inheritance, complete IntPtr value-field dependency, protected access and disposal PASS' \
    --type System.IDisposable --type System.Runtime.InteropServices.SafeHandle \
    --type Microsoft.Win32.SafeHandles.SafeHandleZeroOrMinusOneIsInvalid
}

step_test-array-initializers() {
  compile "$(cygpath -m "$src/tests/array-initializers.cpp")" --target library \
    --type System.Type --type System.RuntimeTypeHandle --type System.Int32 -o "$(cygpath -m "$testdir/array-initializers.dll")"
  mkdir -p "$testdir/array-initializers-consumer"
  cp "$src/tests/ArrayInitializersConsumer.cs" "$testdir/array-initializers-consumer/Program.cs"
  cat > "$testdir/array-initializers-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="array-initializers"><HintPath>../array-initializers.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/array-initializers-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/array-initializers-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case array-init-wrong-element 'public ref class C { static array<int>^ values={gcnew System::Object()}; }; int main() { return 0; }' 'cannot initialize'
}

step_test-array-marshal() {
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local args=(--type System.Runtime.InteropServices.ComInterfaceType --type System.Runtime.InteropServices.UnmanagedType
    --type System.Runtime.InteropServices.GuidAttribute --type System.Runtime.InteropServices.InterfaceTypeAttribute
    --type System.Runtime.InteropServices.ClassInterfaceType --type System.Runtime.InteropServices.ClassInterfaceAttribute
    --type System.Runtime.InteropServices.ComVisibleAttribute
    --reference "$(cygpath -m "${refs[-1]}/System.Runtime.InteropServices.dll")")
  compile "$(cygpath -m "$src/tests/array-marshal.cpp")" --target library "${args[@]}" -o "$(cygpath -m "$testdir/array-marshal.dll")"
  mkdir -p "$testdir/array-marshal-consumer"
  /clang64/bin/clang++.exe -std=c++17 -O2 -shared "$src/tests/array-marshal-native.cpp" -o "$testdir/array-marshal-consumer/array-marshal-native.dll"
  cp "$src/tests/ArrayMarshalConsumer.cs" "$testdir/array-marshal-consumer/Program.cs"
  cat > "$testdir/array-marshal-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="array-marshal"><HintPath>../array-marshal.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/array-marshal-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  cp "$testdir/array-marshal-consumer/array-marshal-native.dll" "$testdir/array-marshal-consumer/bin/Debug/net8.0/"
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/array-marshal-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case array-marshal-non-array 'public interface class I { void F([MarshalAs(System::Runtime::InteropServices::UnmanagedType::LPArray,SizeParamIndex=1)] int value,int count); }; int main() { return 0; }' 'LPArray requires a managed array' "${args[@]}"
  reject_case array-marshal-index 'public interface class I { void F([MarshalAs(System::Runtime::InteropServices::UnmanagedType::LPArray,SizeParamIndex=2)] array<int>^ value,int count); }; int main() { return 0; }' 'SizeParamIndex does not name a valid parameter' "${args[@]}"
  reject_case array-marshal-size-type 'public interface class I { void F([MarshalAs(System::Runtime::InteropServices::UnmanagedType::LPArray,SizeParamIndex=1)] array<int>^ value,double count); }; int main() { return 0; }' 'size parameter must have an integral type' "${args[@]}"
  reject_case array-marshal-negative 'public interface class I { void F([MarshalAs(System::Runtime::InteropServices::UnmanagedType::LPArray,SizeConst=-1)] array<int>^ value); }; int main() { return 0; }' 'SizeConst is outside' "${args[@]}"
  reject_case array-marshal-nonconstant 'int count=3; public interface class I { void F([MarshalAs(System::Runtime::InteropServices::UnmanagedType::LPArray,SizeConst=count)] array<int>^ value); }; int main() { return 0; }' 'constant expression' "${args[@]}"
  reject_case array-marshal-overflow 'public interface class I { void F([MarshalAs(System::Runtime::InteropServices::UnmanagedType::LPArray,SizeConst=4294967299ULL)] array<int>^ value); }; int main() { return 0; }' 'SizeConst is outside' "${args[@]}"
  reject_case array-marshal-index-overflow 'public interface class I { void F([MarshalAs(System::Runtime::InteropServices::UnmanagedType::LPArray,SizeParamIndex=4294967297ULL)] array<int>^ value,int count); }; int main() { return 0; }' 'SizeParamIndex does not name a valid parameter' "${args[@]}"
  reject_case array-marshal-duplicate 'public interface class I { void F([MarshalAs(System::Runtime::InteropServices::UnmanagedType::LPArray,SizeConst=1,SizeConst=2)] array<int>^ value); }; int main() { return 0; }' 'duplicate MarshalAs named option' "${args[@]}"
  reject_case array-marshal-scalar-option 'public interface class I { void F([MarshalAs(System::Runtime::InteropServices::UnmanagedType::I4,SizeConst=3)] int value); }; int main() { return 0; }' 'array options require LPArray' "${args[@]}"
  log "PASS test-array-marshal: real CLR native array marshalling and invalid-input rejection"
}
step_test-each() {
  log "For each collection/array iteration with real CLR finally cleanup"
  mkdir -p "$testdir/each-fixture" "$testdir/each-consumer"
  cp "$src/tests/EachFixture.cs" "$testdir/each-fixture/Fixture.cs"
  cat > "$testdir/each-fixture/each-fixture.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/each-fixture/each-fixture.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  cp "$testdir/each-fixture/bin/Debug/net8.0/each-fixture.dll" "$testdir/each-fixture.dll"
  local args=(--type System.InvalidOperationException --type System.CharEnumerator --type System.Collections.IEnumerable --type System.Collections.IEnumerator
    --type 'System.Collections.Generic.IEnumerable`1' --type 'System.Collections.Generic.IEnumerator`1'
    --type 'System.Collections.Generic.KeyValuePair`2' --type System.IDisposable --type EachFixtures.ProbeSequence --type EachFixtures.ExplicitSequence
    --reference "$(cygpath -m "$testdir/each-fixture.dll")")
  compile "$(cygpath -m "$src/tests/each.cpp")" --target library "${args[@]}" -o "$(cygpath -m "$testdir/each.dll")"
  cp "$src/tests/EachConsumer.cs" "$testdir/each-consumer/Program.cs"
  cat > "$testdir/each-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="each"><HintPath>../each.dll</HintPath></Reference><Reference Include="each-fixture"><HintPath>../each-fixture.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/each-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/each-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case each-reference 'using namespace System; int main() { array<int>^ a = gcnew array<int>(1); for each (int% value in a) { value = 0; } return 0; }' 'tracking-reference for each'
  reject_case each-unbraced 'using namespace System; int main() { array<int>^ a = gcnew array<int>(1); for each (int value in a) Console::WriteLine(value); return 0; }' 'requires a braced body'
  reject_case each-element-type 'using namespace System; public ref class C {}; int main() { array<int>^ a = gcnew array<int>(1); for each (C^ value in a) {} return 0; }' 'matching value handle or an Object handle'
  reject_case each-invalid-next 'public ref class E { public: int MoveNext() { return 0; } property int Current { int get() { return 0; } } }; public ref class C { public: E^ GetEnumerator() { return gcnew E(); } }; int main() { C^ source = gcnew C(); for each (int value in source) {} return 0; }' 'MoveNext must return bool'
  log "PASS test-each: array/generic/nongeneric iteration, value copies and exception/return cleanup"
}
step_test-named-overrides() {
  log "Named CLR interface slots and renamed property accessor implementations"
  local override_args=(--type System.Collections.IEnumerable --type System.Collections.IEnumerator)
  compile "$(cygpath -m "$src/tests/named-overrides.cpp")" --target library "${override_args[@]}" \
    -o "$(cygpath -m "$testdir/named-overrides.dll")"
  mkdir -p "$testdir/named-override-consumer"
  cat > "$testdir/named-override-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="named-overrides"><HintPath>../named-overrides.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/named-override-consumer/Program.cs" <<'EOF'
using System;
using System.Collections;
var sequence = (IEnumerable)new NamedSequence();
var enumerator = sequence.GetEnumerator();
if (!enumerator.MoveNext() || (int)enumerator.Current != 42 || enumerator.MoveNext()) throw new Exception("named interface dispatch failed");
enumerator.Reset();
if (!enumerator.MoveNext() || (int)enumerator.Current != 42) throw new Exception("named property accessor dispatch failed");
var mapping = typeof(NamedSequence).GetInterfaceMap(typeof(IEnumerable));
if (mapping.TargetMethods[0].Name != "GetEnumerator2" || !mapping.TargetMethods[0].IsPrivate || !mapping.TargetMethods[0].IsFinal)
    throw new Exception("named override method identity or visibility lost");
mapping = typeof(NamedEnumerator).GetInterfaceMap(typeof(IEnumerator));
if (Array.FindIndex(mapping.TargetMethods, m => m.Name == "get_Current2") < 0) throw new Exception("accessor override mapping lost");
var source = new SourceProperties();
if (((IProperty)source).Number != 42 || ((IProperty)source).Read() != 42 || ((IHeaderProperty)source).HeaderNumber != 13 ||
    !typeof(IProperty).GetProperty("Number").GetMethod.IsAbstract || !typeof(IHeaderProperty).IsInterface)
    throw new Exception("source interface properties or interface macro protection failed");
Console.WriteLine("named CLR interface overrides OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/named-override-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/named-override-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case named-override-return 'using namespace System::Collections; public ref class C : IEnumerable { virtual int Other() = IEnumerable::GetEnumerator { return 0; } }; int main() { return 0; }' 'does not match the interface member signature' "${override_args[@]}"
  reject_case named-override-unimplemented 'using namespace System::Collections; public ref class C { virtual IEnumerator^ Other() = IEnumerable::GetEnumerator { return nullptr; } }; int main() { return 0; }' 'not an implemented interface' "${override_args[@]}"
  log "PASS test-named-overrides: explicit CLR mappings preserve source names and accessor dispatch"
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
step_test-scalar-boxes() {
  run_case scalar-boxes "$src/tests/scalar-boxes.cpp" $'42\n7\n2147483648\nTrue\nTrue\n17\n19\n0\nTrue\nFalse\n24\n10\n10\nnull box OK\nwrong box OK' \
    --type System.GC --type System.NullReferenceException --type System.InvalidCastException
  mkdir -p "$testdir/scalar-boxes-consumer"
  cp "$src/tests/ScalarBoxesConsumer.cs" "$testdir/scalar-boxes-consumer/Program.cs"
  cat > "$testdir/scalar-boxes-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="scalar-boxes"><HintPath>../scalar-boxes.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/scalar-boxes-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/scalar-boxes-consumer/bin/Debug/net8.0/consumer.dll")"
  cat > "$testdir/scalar-boxes-imported.cpp" <<'EOF'
#using "scalar-boxes.dll"
using namespace System;
int main() {
    Int32^ box=7;
    Int32^ alias=ScalarBoxes::Echo(box);
    *alias=29;
    Console::WriteLine(*box);
    ScalarFlags^ flag=ScalarFlags::High;
    ScalarFlags^ copy=ScalarBoxes::EchoEnum(flag);
    Console::WriteLine(static_cast<unsigned int>(*copy));
    return 0;
}
EOF
  run_case scalar-boxes-imported "$testdir/scalar-boxes-imported.cpp" $'29\n2147483648' --type ScalarBoxes --type ScalarFlags --type ScalarBoxSlot
  reject_case scalar-wrong-native-pointer 'int main() { int value=42; int* pointer=&value; System::Int32^ box=pointer; return 0; }' 'no viable conversion'
  reject_case scalar-wrong-box 'int main() { System::Int32^ i=42; System::Double^ d=i; return 0; }' 'no viable conversion'
  reject_case scalar-native-enum 'enum class Native { One=1 }; int main() { Native^ value=nullptr; return 0; }' 'scalar caret must resolve'
  reject_case scalar-box-native-storage 'typedef System::Int32^ Box; int main() { Box* pointer=nullptr; return 0; }' 'pointers to typed boxed handles'
}
step_test-boxed-values() {
  log "Boxed value handles, alias/mutation semantics and WPF DateTime pattern"
  run_case boxed-values "$src/tests/boxed-values.cpp" $'4\n7\n9\n12\n12\n0\n5\nnull box OK\n504911232000000000' \
    --type System.DateTime --type System.GC --type System.NullReferenceException
  mkdir -p "$testdir/boxed-consumer"
  cat > "$testdir/boxed-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="boxed-values"><HintPath>../boxed-values.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/boxed-consumer/Program.cs" <<'EOF'
using System;
using System.Linq;
using Example;
ValueType box = Boxes.Make(6);
if (box is not BoxPoint point || point.X != 6 || Boxes.Read(box) != 6) throw new Exception("incorrect box identity");
Boxes.Move(box);
if (((BoxPoint)box).X != 8 || Boxes.Read(box) != 8) throw new Exception("boxed mutation copied the value");
var api = typeof(Boxes).GetMethod("Make");
var modifiers = api.ReturnParameter.GetOptionalCustomModifiers();
if (api.ReturnType != typeof(ValueType) || !modifiers.Contains(typeof(BoxPoint)) || !modifiers.Any(t => t.FullName == "System.Runtime.CompilerServices.IsBoxed"))
    throw new Exception("missing typed-box metadata");
if (Boxes.FileTime(0) != DateTime.FromFileTimeUtc(0).Ticks) throw new Exception("incorrect imported DateTime boxing");
Console.WriteLine("boxed API metadata and C# calls OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/boxed-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/boxed-consumer/bin/Debug/net8.0/consumer.dll")"
  cat > "$testdir/boxed-reference.cpp" <<'EOF'
#using "boxed-values.dll"
using namespace System;
int main() { Example::BoxPoint^ value = Example::Boxes::Make(6); Example::Boxes::Move(value); Console::WriteLine(Example::Boxes::Read(value)); return 0; }
EOF
  run_case boxed-reference "$testdir/boxed-reference.cpp" '8' --type Example.Boxes --type Example.BoxPoint
  reject_case wrong-typed-box 'public value struct A { int X; }; public value struct B { int X; }; int main() { A value; B^ box = value; return 0; }' 'no viable conversion'
  log "PASS test-boxed-values: CLR boxes, field/method mutation and imported value boxing"
}
step_test-static-fields() {
  log "Managed static fields and CLR type-initializer ordering/locking/failure"
  compile "$(cygpath -m "$src/tests/static-fields.cpp")" --target library --std c++14 \
    --type System.InvalidOperationException -o "$(cygpath -m "$testdir/static-fields.dll")"
  mkdir -p "$testdir/static-consumer"
  cat > "$testdir/static-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="static-fields"><HintPath>../static-fields.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/static-consumer/Program.cs" <<'EOF'
using System;
using System.Reflection;
using System.Threading.Tasks;
using Example;
var locks = new object[32];
Parallel.For(0, locks.Length, i => { locks[i] = State.Lock(); if (State.Read() != 42) throw new Exception("incorrect initialization order"); });
GC.Collect(2, GCCollectionMode.Forced, true, true);
if (State.Raw() != "RAW" || State.Unicode() != "東京 😀") throw new Exception("literal static String initializer or adjacent UTF16 literals lost");
foreach (var value in locks) if (!ReferenceEquals(value, locks[0])) throw new Exception("static initializer ran repeatedly");
Parallel.For(0, locks.Length, i => { if (Explicit.Read() != 43 || Explicit.Runs() != 1) throw new Exception("explicit static constructor ordering/once semantics lost"); });
if (OutOfLine.Read() != 42 || ValueInitialization.Read() != 42 || new Explicit(99).Snapshot != 99 || !typeof(Explicit).TypeInitializer.IsPrivate)
    throw new Exception("out-of-line/value static constructor or instance constructor lost");
State.Increment();
if (State.Read() != 43 || typeof(State).TypeInitializer == null) throw new Exception("static field byref or cctor metadata failed");
if (!typeof(State).GetField("_seed", BindingFlags.Static | BindingFlags.NonPublic).IsPrivate) throw new Exception("static field visibility lost");
for (int i = 0; i < 2; ++i) {
    try { Failing.Read(); throw new Exception("failed initializer did not poison the type"); }
    catch (TypeInitializationException e) when (e.InnerException is InvalidOperationException) { }
    try { FailingExplicit.Read(); throw new Exception("failed explicit initializer did not poison the type"); }
    catch (TypeInitializationException e) when (e.InnerException is InvalidOperationException) { }
}
Console.WriteLine("static fields and CLR initialization OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/static-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/static-consumer/bin/Debug/net8.0/consumer.dll")" > "$testdir/static-consumer.stdout"
  tr -d '\r' < "$testdir/static-consumer.stdout" > "$testdir/static-consumer.actual"
  printf '%s\n' 'initialize once' 'static fields and CLR initialization OK' > "$testdir/static-consumer.expected"
  cmp "$testdir/static-consumer.expected" "$testdir/static-consumer.actual"
  cat "$testdir/static-consumer.actual"
  reject_case cctor-parameters 'public ref class C { static C(int value) {} }; int main() { return 0; }' 'static constructor takes no parameters'
  reject_case cctor-missing 'public ref class C { static C(); }; int main() { return 0; }' 'static constructor requires a definition'
  reject_case cctor-initializer 'public ref class C { int Value; static C() : Value(1) {} }; int main() { return 0; }' 'static constructor cannot have an initializer list'
  log "PASS test-static-fields: ordered CLR .cctor and private static storage"
}
step_test-static-properties() {
  log "Static CLI properties, out-of-line accessors and assembly visibility"
  run_case static-properties "$src/tests/static-properties.cpp" $'40\n42\n11'
  mkdir -p "$testdir/static-property-consumer"
  cat > "$testdir/static-property-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><AssemblyName>static-property-fixture</AssemblyName><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="static-properties"><HintPath>../static-properties.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/static-property-consumer/Program.cs" <<'EOF'
using System;
using System.Reflection;
using Example;
if (StaticProperties.Value != 40 || StaticProperties.Text != "static text") throw new Exception("incorrect static property metadata");
if (StaticProperties.Automatic != 0 || !typeof(StaticProperties).GetProperty("Automatic").GetMethod.IsStatic)
    throw new Exception("static auto-property default or accessor identity lost");
StaticProperties.Automatic=43;
if (StaticProperties.Automatic != 43) throw new Exception("static auto-property C# mutation lost");
StaticProperties.Value = 99;
if (StaticProperties.Value != 99 || StaticProperties.ReadInternal() != 99) throw new Exception("static setter lost storage");
var internalType = typeof(StaticProperties).Assembly.GetType("Example.InternalState");
if (internalType.IsPublic || !internalType.GetMethod("Read", BindingFlags.Static | BindingFlags.NonPublic).IsAssembly ||
    !internalType.GetProperty("Value", BindingFlags.Static | BindingFlags.NonPublic).GetMethod.IsAssembly)
    throw new Exception("internal type/accessor visibility lost");
Console.WriteLine("static properties and internal visibility OK");
public class StaticAccessorAccess {
    public static int ReadOnly { get; protected set; } = 7;
    public static int WriteOnly { protected get; set; } = 8;
    protected static int Hidden { get; set; } = 9;
    public static int ReadWriteOnly() => WriteOnly;
}
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/static-property-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/static-property-consumer/bin/Debug/net8.0/static-property-fixture.dll")"
  cat > "$testdir/static-property-reference.cpp" <<'EOF'
#using "static-properties.dll"
using namespace System;
int main() {
    if(Example::StaticProperties::Automatic!=0) return 1;
    Example::StaticProperties::Automatic=44;
    if(Example::StaticProperties::Automatic!=44) return 2;
    Console::WriteLine(Example::StaticProperties::Value);
    Example::StaticProperties::Value = 61;
    Console::WriteLine(Example::StaticProperties::Value);
    Console::WriteLine(Example::StaticProperties::Text->Length);
    return 0;
}
EOF
  run_case static-property-reference "$testdir/static-property-reference.cpp" $'40\n61\n11' --type Example.StaticProperties
  local refs=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local args=(--type System.Threading.Thread --type System.Globalization.CultureInfo
    --reference "$(cygpath -m "${refs[-1]}/System.Threading.Thread.dll")" --reference "$(cygpath -m "${refs[-1]}/System.Globalization.dll")")
  cat > "$testdir/framework-static-properties.cpp" <<'EOF'
using namespace System;
int main() {
    System::Threading::Thread^ current = System::Threading::Thread::CurrentThread;
    Console::WriteLine(current != nullptr);
    Console::WriteLine(current->ManagedThreadId > 0);
    Console::WriteLine(System::Globalization::CultureInfo::InvariantCulture->Name->Length == 0);
    return 0;
}
EOF
  run_case framework-static-properties "$testdir/framework-static-properties.cpp" $'True\nTrue\nTrue' "${args[@]}"
  reject_case imported-static-property-readonly '#using "static-properties.dll"
int main() { Example::StaticProperties::Text = __cli_literal("changed"); return 0; }' 'static property has no setter' --type Example.StaticProperties
  cp "$testdir/static-property-consumer/bin/Debug/net8.0/static-property-fixture.dll" "$testdir/"
  cat > "$testdir/static-property-access.cpp" <<'EOF'
#using "static-property-fixture.dll"
using namespace System;
int main() { Console::WriteLine(StaticAccessorAccess::ReadOnly); StaticAccessorAccess::WriteOnly = 12; Console::WriteLine(StaticAccessorAccess::ReadWriteOnly()); return 0; }
EOF
  run_case static-property-access "$testdir/static-property-access.cpp" $'7\n12' --type StaticAccessorAccess
  reject_case static-property-protected-set '#using "static-property-fixture.dll"
int main() { StaticAccessorAccess::ReadOnly = 10; return 0; }' 'static property has no setter' --type StaticAccessorAccess
  reject_case static-property-protected-get '#using "static-property-fixture.dll"
int main() { int value = StaticAccessorAccess::WriteOnly; return value; }' 'static property has no getter' --type StaticAccessorAccess
  reject_case static-property-protected-method '#using "static-property-fixture.dll"
int main() { return StaticAccessorAccess::get_Hidden(); }' 'protected member' --type StaticAccessorAccess
  log "PASS test-static-properties: CLR static properties, imported read/write and framework getters"
}
step_test-interior-records() {
  compile "$(cygpath -m "$src/tests/interior-records.cpp")" --target library --type System.GC \
    -o "$(cygpath -m "$testdir/interior-records.dll")"
  mkdir -p "$testdir/interior-records-consumer"
  cp "$src/tests/InteriorRecordsConsumer.cs" "$testdir/interior-records-consumer/Program.cs"
  cat > "$testdir/interior-records-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="interior-records"><HintPath>../interior-records.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/interior-records-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/interior-records-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case interior-arrow-escape 'struct Native { int Value; }; int main() { Native value; interior_ptr<Native> data=&value; Native* raw=data.operator->(); return 0; }' 'cannot escape as raw native pointers'
  reject_case interior-array-escape 'struct Native { int Values[2]; }; int main() { Native value; interior_ptr<Native> data=&value; int* raw=data->Values; return 0; }' 'require pinning before raw native pointer binding'
  reject_case interior-record-gc 'struct Bad { System::String^ Text; }; void Read(interior_ptr<Bad> data) {} int main() { return 0; }' 'GC-free native POD elements'
  reject_case interior-view-const 'int main() { const int value=1; interior_ptr<const int> data=&value; interior_ptr<int> writable=(interior_ptr<int>)data; return 0; }' 'cannot discard element qualifiers'
  reject_case interior-view-volatile 'int main() { volatile int value=1; interior_ptr<volatile int> data=&value; interior_ptr<int> writable=(interior_ptr<int>)data; return 0; }' 'cannot discard element qualifiers'
  reject_case interior-view-native 'int main() { int* raw=nullptr; interior_ptr<unsigned char> data=(interior_ptr<unsigned char>)raw; return 0; }' 'not an untracked native pointer'
}
step_test-interior-indexing() {
  compile "$(cygpath -m "$src/tests/interior-indexing.cpp")" --target library --type System.GC \
    -o "$(cygpath -m "$testdir/interior-indexing.dll")"
  mkdir -p "$testdir/interior-indexing-consumer"
  cp "$src/tests/InteriorIndexingConsumer.cs" "$testdir/interior-indexing-consumer/Program.cs"
  cat > "$testdir/interior-indexing-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="interior-indexing"><HintPath>../interior-indexing.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/interior-indexing-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/interior-indexing-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case interior-index-const 'int main() { const wchar_t* native=L"text"; interior_ptr<const wchar_t> chars=PtrToStringChars(__cli_literal(L"text")); chars[0]=65; return 0; }' 'returns a const value'
}
step_test-nativeints() {
  log "CLR native integers, primitive pointer metadata and WPF Marshal.Copy pattern"
  local references=("$CPPCLI_SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  run_case nativeints "$src/tests/nativeints.cpp" $'5000000002\n0\n8721' --type System.Runtime.InteropServices.Marshal \
    --reference "$(cygpath -m "${references[-1]}/System.Runtime.InteropServices.dll")"
  mkdir -p "$testdir/nativeint-consumer"
  cat > "$testdir/nativeint-consumer/consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="nativeints"><HintPath>../nativeints.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/nativeint-consumer/Program.cs" <<'EOF'
using System;
if (NativeInts.Make(5000000002L) != new IntPtr(5000000002L) || NativeInts.Bits(new IntPtr(-7)) != -7 || NativeInts.Zero() != IntPtr.Zero || NativeInts.Copy() != 8721)
    throw new Exception("native integer ABI or Marshal.Copy failed");
if (NativeInts.Unsigned(0xffffffffffffffffUL) != new UIntPtr(0xffffffffffffffffUL) || NativeInts.UnsignedBits(new UIntPtr(5000000002UL)) != 5000000002UL)
    throw new Exception("unsigned native integer width lost");
var api = typeof(NativeInts);
if (api.GetMethod("Make").ReturnType != typeof(IntPtr) || api.GetMethod("Bits").GetParameters()[0].ParameterType != typeof(IntPtr))
    throw new Exception("native integer signature width/type lost");
if(NativeInts.Raw(new IntPtr(-1))!=ulong.MaxValue || NativeInts.Receivers!=1 || NativeInts.RawUnsigned(new UIntPtr(0x8000000000000001UL))!=0x8000000000000001UL || NativeInts.TypedCast()!=0x44332211)
    throw new Exception("native integer pointer cast width, receiver evaluation or memory write");
Console.WriteLine("native integer API metadata and pointer casts OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/nativeint-consumer/consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/nativeint-consumer/bin/Debug/net8.0/consumer.dll")"
  reject_case readonly-imported-static 'using namespace System; int main() { IntPtr::Zero = IntPtr(1); return 0; }' 'readonly imported fields cannot be assigned'
  reject_case nativeint-implicit-pointer 'int main() { void* pointer=System::IntPtr(42); return 0; }' 'ambiguous'
  reject_case nativeint-managed-target 'int main() { System::String^ value=static_cast<System::String^>(System::IntPtr(42)); return 0; }' 'raw native pointer target'
  log "PASS test-nativeints: pointer-sized signatures and native memory copying"
}
step_test-tracking() {
  run_case tracking "$src/tests/tracking.cpp" $'8\n10\n23\n5\n6\nTrue\n2' --type System.GC
  mkdir -p "$testdir/tracking-consumer"
  cat > "$testdir/tracking-consumer/tracking-consumer.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><ItemGroup><Reference Include="tracking"><HintPath>../tracking.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$testdir/tracking-consumer/Program.cs" <<'EOF'
using System;
using Example;
int n = 7;
Tracking.Change(ref n);
var p = new Coordinate(4);
Tracking.Move(ref p);
object o = null;
Tracking.Replace(ref o);
if (n != 10 || p.X != 5 || (string)o != "replaced" || !typeof(Tracking).GetMethod("Change").GetParameters()[0].ParameterType.IsByRef)
    throw new Exception("incorrect tracking references");
Console.WriteLine("tracking reference metadata OK");
EOF
  "$CPPCLI_SDK/dotnet.exe" build "$(cygpath -m "$testdir/tracking-consumer/tracking-consumer.csproj")" -nologo -v:q \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -p:NuGetAudit=false
  "$CPPCLI_SDK/dotnet.exe" "$(cygpath -m "$testdir/tracking-consumer/bin/Debug/net8.0/tracking-consumer.dll")"
  cat > "$testdir/tracking-reference.cpp" <<'EOF'
#using "tracking.dll"
using namespace System;
int main() { int n = 5; Example::Tracking::Change(n); Console::WriteLine(n); return 0; }
EOF
  run_case tracking-reference "$testdir/tracking-reference.cpp" 8 --type Example.Tracking --type Example.Coordinate
  reject_case native-reference 'public ref class Owner { public: int Value; }; void f(int& n) {} void Bad(Owner^ owner) { f(owner->Value); } int main() { return 0; }' 'native reference cannot bind to movable managed storage'
  reject_case reference-return 'int% f(int% n) { return n; } int main() { return 0; }' 'returning tracking references'
}
for step in $steps; do "step_$step"; done

