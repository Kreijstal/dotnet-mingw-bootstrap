#!/bin/bash
# Source-only WPF foundations. Rendering and the C++/CLI bridges remain pending.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
version=8.0.0
source=${WPF_SOURCE:-$M612/src/wpf-$version}
build=${WPF_BUILD:-$M612/build/wpf-$version}
sdk=${WPF_SDK:-/clang64/lib/dotnet}
oob=${WPF_OOB:-$M612/oob-$version/lib}
steps=${*:-fetch prerequisites foundations native-win32 check-foundations inventory}
mkdir -p "$build/lib" "$build/keys"

step_fetch() {
  local archive=$M612/src/wpf-$version.tar.gz
  local hash=b4acc515805fd551fe9982f496e4062f0e9ddc4bed219a236454857b56126748
  [ -f "$archive" ] || curl -fL "https://codeload.github.com/dotnet/wpf/tar.gz/refs/tags/v$version" -o "$archive"
  echo "$hash  $archive" | sha256sum -c -
  if [ ! -d "$source/src/Microsoft.DotNet.Wpf/src" ]; then
    mkdir -p "$source"
    tar -xf "$archive" --strip-components=1 -C "$source"
  fi
  scan_binaries "$source" > "$build/deleted-binaries.txt"
  /usr/bin/python3 - "$build/deleted-binaries.txt" <<'PYCLEAN'
from pathlib import Path
import sys
for line in Path(sys.argv[1]).read_text().splitlines():
    Path(line).unlink()
PYCLEAN
}

initialize() {
  printf '<configuration><packageSources><clear /></packageSources></configuration>\n' > "$build/NuGet.config"
  /usr/bin/python3 - "$here/keys" "$build/keys" <<'PYKEYS'
from pathlib import Path
import sys
source,dest=map(Path,sys.argv[1:])
for name in ('ECMA','MSShared'):
    (dest/(name+'.snk')).write_bytes(bytes.fromhex(next(source.glob(name+'-*.pubkey.hex')).read_text()))
PYKEYS
}

step_prerequisites() {
  CSC_DOTNET="$sdk/dotnet.exe" OOB='System.IO.Packaging System.Security.Permissions' \
    bash "$here/libraries.sh" "$sdk/sdk/8.0.131/Roslyn/bincore/csc.dll" plan gens build report
  for name in System.IO.Packaging System.Security.Permissions; do test -s "$oob/$name.dll"; done
}

build_assembly() {
  local name=$1
  initialize
  /usr/bin/python3 "$here/tools/wpfplan.py" "$source" "$build" "$sdk" "$oob" "$build/keys" "$name"
  "$sdk/dotnet.exe" build "$(cygpath -m "$build/$name/$name.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -v:q \
    -p:UseSharedCompilation=false -p:NuGetAudit=false > "$build/$name/build.log" 2>&1 || {
      cat "$build/$name/build.log"
      die "WPF source build failed: $name"
    }
  cp "$build/$name/bin/Release/net8.0/$name.dll" "$build/lib/"
  log "Built $name from source"
}

step_foundations() {
  for name in System.Windows.Input.Manipulations System.Xaml WindowsBase UIAutomationTypes UIAutomationProvider; do
    build_assembly "$name"
  done
}

step_native_win32() {
  /clang64/bin/clang++.exe -O2 -shared "$here/wpf-native/PresentationNativeWin32.cpp" \
    -o "$build/lib/PresentationNative_cor3.dll" -luser32 -lgdi32
}

step_inventory() {
  /usr/bin/python3 "$here/tools/wpfnative-inventory.py" "$source" "$build/lib/PresentationNative_cor3.dll" \
    "$build/native-coverage.json"
}

bridge_compile() {
  local compiler=(cli-clang)
  if [ -n "${WPF_CPPCLI_BUILD:-}" ]; then
    compiler=(/clang64/bin/python.exe "$(cygpath -m "$WPF_CPPCLI_BUILD/cli-clang")" \
      --sdk "$(cygpath -m "$sdk")" --clang "$(cygpath -m /clang64/bin/clang++.exe)")
  fi
  "${compiler[@]}" "$@"
}

step_prepare_bridges() {
  local patchfile
  for patchfile in "$here"/patches/wpf/*.patch; do
    if ! patch --dry-run -R -p1 -d "$source" -i "$patchfile" >/dev/null 2>&1; then
      patch -p1 -d "$source" -i "$patchfile"
    fi
  done
}

step_check_checked_integers() {
  step_prepare_bridges
  local inc=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/TrueTypeSubsetter/TtfDelta
  /clang64/bin/clang++.exe -std=c++14 -O2 "$here/wpf-tests/CheckedIntegers.cpp" -I "$inc" -o "$build/checked-integers.exe"
  "$build/checked-integers.exe"
}

step_bridge_gate() {
  step_prepare_bridges
  local cpp=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP
  local shared=$source/src/Microsoft.DotNet.Wpf/src/Shared/inc
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  bridge_compile "$(cygpath -m "$cpp/DirectWriteForwarder.cpp")" --std c++14 --target library --emit il \
    --public-key "$(cygpath -m "$build/keys/MSShared.snk")" \
    --type System.Double --type System.Int32 --type System.UInt32 --type System.IFormatProvider --type System.Globalization.NumberStyles \
    --type System.DateTime --type System.Threading.Monitor --type System.Security.SecurityCriticalAttribute \
    --type System.Uri --type System.IO.FileFormatException --type System.IntPtr --type System.Runtime.InteropServices.Marshal \
    --type System.Guid --type System.Exception --type System.IO.FileNotFoundException \
    --type System.UnauthorizedAccessException --type System.Net.WebException \
    --type System.InvalidOperationException --type System.Windows.Point --type System.Windows.Media.TextFormattingMode \
    --type System.Runtime.InteropServices.CriticalHandle --type System.Threading.Interlocked --type System.GC \
    --type System.Runtime.ConstrainedExecution.CriticalFinalizerObject \
    --type System.Runtime.ConstrainedExecution.Cer --type System.Runtime.ConstrainedExecution.Consistency \
    --type System.Runtime.ConstrainedExecution.ReliabilityContractAttribute --type System.Globalization.CultureInfo \
    --type System.NotSupportedException --type System.NotImplementedException --type System.IO.Stream --type System.IO.UnmanagedMemoryStream --type MS.Internal.Invariant \
    --type 'System.Collections.Generic.IDictionary`2' --type 'System.Collections.Generic.ICollection`1' \
    --type 'System.Collections.Generic.IEnumerable`1' --type 'System.Collections.Generic.IEnumerator`1' \
    --type 'System.Collections.Generic.KeyValuePair`2' --type System.Collections.IEnumerable \
    --type 'System.Collections.Generic.List`1' --type 'System.Collections.Generic.IList`1' \
    --type 'System.Collections.Generic.Dictionary`2' \
    --type System.Delegate --type System.MulticastDelegate \
    --type System.Collections.IEnumerator --type System.IDisposable \
    --type System.Runtime.InteropServices.StructLayoutAttribute --type System.Runtime.InteropServices.LayoutKind \
    --type System.Runtime.InteropServices.FieldOffsetAttribute \
    --type System.Runtime.InteropServices.ComInterfaceType --type System.Runtime.InteropServices.UnmanagedType \
    --type System.Runtime.InteropServices.GuidAttribute --type System.Runtime.InteropServices.InterfaceTypeAttribute \
    --type System.Runtime.InteropServices.ClassInterfaceAttribute --type System.Runtime.InteropServices.ClassInterfaceType \
    --type System.Runtime.InteropServices.ComVisibleAttribute \
    --type System.Diagnostics.Debug \
    --type System.Type --type System.RuntimeTypeHandle --type System.ArgumentOutOfRangeException --type System.OverflowException \
    --type System.Runtime.InteropServices.ComTypes.FILETIME --type System.Threading.Thread \
    --type System.Windows.Threading.Dispatcher --type System.Windows.Threading.DispatcherOperation \
    --type System.Action --type System.Math --type System.Runtime.InteropServices.GCHandle --type System.Runtime.InteropServices.GCHandleType \
    --reference "$(cygpath -m "$core_ref/System.Threading.dll")" \
    --reference "$(cygpath -m "$core_ref/System.Threading.Thread.dll")" \
    --reference "$(cygpath -m "$core_ref/System.Collections.dll")" \
    --reference "$(cygpath -m "$core_ref/System.Runtime.InteropServices.dll")" \
    --reference "$(cygpath -m "$core_ref/System.Net.Requests.dll")" \
    --reference "$(cygpath -m "$core_ref/System.Globalization.dll")" \
    --reference "$(cygpath -m "$oob/System.IO.Packaging.dll")" \
    --reference "$(cygpath -m "$build/lib/WindowsBase.dll")" -I "$(cygpath -m "$shared")" \
    -I "$(cygpath -m "$cpp/..")" -I "$(cygpath -m "$cpp/TrueTypeSubsetter")" \
    -D '__field_bcount(x)=_Field_size_bytes_(x)' -D '__field_ecount(x)=_Field_size_(x)' \
    -D '__field_range(low,high)=_Field_range_(low,high)' -D '__deref_out_range(low,high)=_Deref_out_range_(low,high)' \
    -D '__success(x)=_Success_(x)' \
    -D '__out=_Out_' -D '__deref_out_bcount(x)=_Outptr_result_bytebuffer_(x)' \
    -D '__deref_inout_ecount(x)=_Inout_updates_(x)' -D '__inout_ecount(x)=_Inout_updates_(x)' \
    -D '__in_ecount(x)=_In_reads_(x)' -D '__out_ecount(x)=_Out_writes_(x)' \
    -D "WINDOWS_BASE_DLL=\"$(cygpath -m "$build/lib/WindowsBase.dll")\"" \
    -D UNICODE=1 -D _UNICODE=1 -D PRESENTATION_CORE=1 -D _CLR_NETCORE=1 -o "$(cygpath -m "$build/DirectWriteForwarder.il")" \
    > "$build/directwrite-gate.log" 2>&1 || {
      cat "$build/directwrite-gate.log"
      die "The frontend cannot compile the upstream DirectWriteForwarder bridge yet"
    }
}

step_check_directwrite_bridge() {
  initialize
  local work=$build/directwrite-check
  mkdir -p "$work"
  /clang64/bin/clang++.exe -std=c++17 -fms-extensions -shared -O2 "$here/wpf-native/DirectWriteBridge.cpp" \
    -o "$work/wpf-directwrite-native.dll" -ldwrite
  bridge_compile "$(cygpath -m "$here/wpf-tests/DirectWriteBridge.cpp")" --target library \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --native-library wpf-directwrite-native.dll \
    -o "$(cygpath -m "$work/SourceDirectWriteBridge.dll")"
  cp "$here/wpf-tests/DirectWriteBridge.cs" "$work/Program.cs"
  cat > "$work/DirectWriteCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="SourceDirectWriteBridge"><HintPath>SourceDirectWriteBridge.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/DirectWriteCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$work/wpf-directwrite-native.dll" "$work/bin/Release/net8.0/"
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/DirectWriteCheck.dll")"
}

step_check_localized_messages() {
  initialize
  local work=$build/localized-messages-check
  local original=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper/LocalizedErrorMsgs.cpp
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  cat > "$work/LocalizedMessages.cpp" <<EOF
using namespace System;
#include "$(cygpath -m "$original")"
public ref class WpfLocalizedMessages abstract sealed {
public:
  static String^ Read() { return MS::Internal::Text::TextInterface::LocalizedErrorMsgs::EnumeratorNotStarted; }
  static void Write(String^ text) { MS::Internal::Text::TextInterface::LocalizedErrorMsgs::EnumeratorNotStarted = text; }
};
EOF
  bridge_compile "$(cygpath -m "$work/LocalizedMessages.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --type System.Threading.Monitor \
    --reference "$(cygpath -m "$core_ref/System.Threading.dll")" -o "$(cygpath -m "$work/LocalizedMessages.dll")"
  cat > "$work/LocalizedMessagesCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="LocalizedMessages"><HintPath>LocalizedMessages.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cat > "$work/Program.cs" <<'EOF'
using System;
using System.Reflection;
using System.Threading.Tasks;
if (WpfLocalizedMessages.Read() != null) throw new Exception("unexpected initial error message");
WpfLocalizedMessages.Write("café 東京 😀");
if (WpfLocalizedMessages.Read() != "café 東京 😀") throw new Exception("Unicode message roundtrip failed");
Parallel.For(0, 64, i => { WpfLocalizedMessages.Write("message " + i); if (WpfLocalizedMessages.Read() == null) throw new Exception("concurrent message access failed"); });
var type = typeof(WpfLocalizedMessages).Assembly.GetType("MS.Internal.Text.TextInterface.LocalizedErrorMsgs");
var property = type.GetProperty("EnumeratorNotStarted", BindingFlags.Static | BindingFlags.NonPublic);
if (type.IsPublic || type.TypeInitializer == null || !property.GetMethod.IsAssembly || !property.SetMethod.IsAssembly)
    throw new Exception("upstream type/accessor visibility or initialization lost");
Console.WriteLine("upstream WPF LocalizedErrorMsgs OK");
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/LocalizedMessagesCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/LocalizedMessagesCheck.dll")"
}

step_check_font_source() {
  initialize
  local work=$build/font-source-check
  local original=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper/IFontSource.h
  mkdir -p "$work"
  cat > "$work/FontSource.cpp" <<EOF
#include <windows.h>
#include "$(cygpath -m "$original")"
public ref class FontSourceHeaderMarker abstract sealed {};
EOF
  bridge_compile "$(cygpath -m "$work/FontSource.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" \
    --type System.DateTime --type System.Uri --type System.IO.UnmanagedMemoryStream \
    -o "$(cygpath -m "$work/FontSource.dll")"
  cp "$here/wpf-tests/FontSource.cs" "$work/Program.cs"
  cat > "$work/FontSourceCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="FontSource"><HintPath>FontSource.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/FontSourceCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/FontSourceCheck.dll")"
}

step_check_factory_tokens() {
  initialize
  local work=$build/factory-token-check
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  /clang64/bin/python.exe "$here/tools/wpf-factory-token-check.py" "$(cygpath -m "$source")" "$(cygpath -m "$work/FactoryTokens.cpp")"
  bridge_compile "$(cygpath -m "$work/FactoryTokens.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --reference "$(cygpath -m "$core_ref/System.Runtime.InteropServices.dll")" \
    --type System.Runtime.InteropServices.CriticalHandle --type System.IntPtr \
    --type System.Runtime.ConstrainedExecution.Consistency --type System.Runtime.ConstrainedExecution.Cer \
    --type System.Runtime.ConstrainedExecution.ReliabilityContractAttribute -o "$(cygpath -m "$work/FactoryTokens.dll")"
  cp "$here/wpf-tests/FactoryTokens.cs" "$work/Program.cs"
  cat > "$work/FactoryTokensCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit><NoWarn>SYSLIB0004</NoWarn></PropertyGroup><ItemGroup><Reference Include="FactoryTokens"><HintPath>FactoryTokens.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/FactoryTokensCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/FactoryTokensCheck.dll")"
}

step_check_delegates() {
  initialize
  local work=$build/delegates-check
  mkdir -p "$work"
  /clang64/bin/python.exe "$here/tools/wpf-delegates-check.py" "$(cygpath -m "$source")" "$(cygpath -m "$work/Delegates.cpp")"
  bridge_compile "$(cygpath -m "$work/Delegates.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --type System.Delegate --type System.MulticastDelegate \
    --type System.InvalidOperationException -o "$(cygpath -m "$work/Delegates.dll")"
  cp "$here/wpf-tests/Delegates.cs" "$work/Program.cs"
  cat > "$work/DelegatesCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><AllowUnsafeBlocks>true</AllowUnsafeBlocks><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="Delegates"><HintPath>Delegates.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/DelegatesCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/DelegatesCheck.dll")"
}

step_check_com_interfaces() {
  initialize
  local work=$build/com-interfaces-check
  local original=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  cat > "$work/ComInterfaces.cpp" <<EOF
#include <dwrite.h>
#include "$(cygpath -m "$original/DWriteInterfaces.h")"
#include "$(cygpath -m "$original/IClassification.h")"
public ref class WpfComHeaderMarker abstract sealed {};
EOF
  bridge_compile "$(cygpath -m "$work/ComInterfaces.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" \
    --reference "$(cygpath -m "$core_ref/System.Runtime.InteropServices.dll")" --type System.IntPtr \
    --type System.Runtime.InteropServices.ComInterfaceType --type System.Runtime.InteropServices.UnmanagedType \
    --type System.Runtime.InteropServices.GuidAttribute --type System.Runtime.InteropServices.InterfaceTypeAttribute \
    -o "$(cygpath -m "$work/ComInterfaces.dll")"
  cp "$here/wpf-tests/ComInterfaces.cs" "$work/Program.cs"
  cat > "$work/ComInterfacesCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="ComInterfaces"><HintPath>ComInterfaces.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/ComInterfacesCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/ComInterfacesCheck.dll")"
}

step_check_localized_copy() {
  initialize
  local work=$build/localized-copy-check
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  /clang64/bin/python.exe "$here/tools/wpf-localized-copy-check.py" "$(cygpath -m "$source")" "$(cygpath -m "$work/LocalizedCopy.cpp")"
  bridge_compile "$(cygpath -m "$work/LocalizedCopy.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" \
    --reference "$(cygpath -m "$core_ref/System.Globalization.dll")" --type System.Globalization.CultureInfo \
    --type 'System.Collections.Generic.IEnumerable`1' --type 'System.Collections.Generic.IEnumerator`1' \
    --type 'System.Collections.Generic.KeyValuePair`2' --type System.Collections.IEnumerable \
    --type System.Collections.IEnumerator --type System.IDisposable -o "$(cygpath -m "$work/LocalizedCopy.dll")"
  cp "$here/wpf-tests/LocalizedCopy.cs" "$work/Program.cs"
  cat > "$work/LocalizedCopyCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="LocalizedCopy"><HintPath>LocalizedCopy.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/LocalizedCopyCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/LocalizedCopyCheck.dll")"
}

step_check_font_simulations() {
  initialize
  local work=$build/font-simulations-check
  mkdir -p "$work"
  /clang64/bin/python.exe "$here/tools/wpf-font-simulations-check.py" "$(cygpath -m "$source")" \
    "$(cygpath -m /clang64/include/dwrite.h)" "$(cygpath -m "$work/FontSimulations.cpp")"
  bridge_compile "$(cygpath -m "$work/FontSimulations.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --type System.InvalidOperationException \
    -o "$(cygpath -m "$work/FontSimulations.dll")"
  cp "$here/wpf-tests/FontSimulations.cs" "$work/Program.cs"
  cat > "$work/FontSimulationsCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="FontSimulations"><HintPath>FontSimulations.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/FontSimulationsCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/FontSimulationsCheck.dll")"
}

step_check_pointer_handle() {
  initialize
  local work=$build/pointer-handle-check
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  /usr/bin/python3 "$here/tools/wpf-pointer-handle-check.py" "$source" "$work/PointerHandle.cpp"
  bridge_compile "$(cygpath -m "$work/PointerHandle.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --type System.IntPtr \
    --type System.Runtime.InteropServices.CriticalHandle --type System.Runtime.ConstrainedExecution.CriticalFinalizerObject \
    --reference "$(cygpath -m "$core_ref/System.Runtime.InteropServices.dll")" -o "$(cygpath -m "$work/PointerHandle.dll")"
  cp "$here/wpf-tests/PointerHandle.cs" "$work/Program.cs"
  cat > "$work/PointerHandleCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="PointerHandle"><HintPath>PointerHandle.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/PointerHandleCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/PointerHandleCheck.dll")"
}

step_check_string_chars() {
  initialize
  local work=$build/string-chars-check
  mkdir -p "$work"
  /usr/bin/python3 "$here/tools/wpf-string-chars-check.py" "$source" "$work/StringChars.cpp"
  bridge_compile "$(cygpath -m "$work/StringChars.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" -o "$(cygpath -m "$work/StringChars.dll")"
  cp "$here/wpf-tests/StringChars.cs" "$work/Program.cs"
  cat > "$work/StringCharsCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="StringChars"><HintPath>StringChars.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/StringCharsCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/StringCharsCheck.dll")"
}

step_check_font_version() {
  initialize
  local work=$build/font-version-check
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  /usr/bin/python3 "$here/tools/wpf-font-version-check.py" "$source" "$work/FontVersion.cpp"
  bridge_compile "$(cygpath -m "$work/FontVersion.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" \
    --type System.Double --type System.Type --type System.IFormatProvider \
    --type System.Globalization.NumberStyles --type System.Globalization.CultureInfo \
    --reference "$(cygpath -m "$core_ref/System.Globalization.dll")" -o "$(cygpath -m "$work/FontVersion.dll")"
  cp "$here/wpf-tests/FontVersion.cs" "$work/Program.cs"
  cat > "$work/FontVersionCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="FontVersion"><HintPath>FontVersion.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/FontVersionCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/FontVersionCheck.dll")"
}

step_check_factory_dispatcher() {
  initialize
  local work=$build/factory-dispatcher-check
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  /usr/bin/python3 "$here/tools/wpf-dispatcher-check.py" "$source" "$work/FactoryDispatch.cpp"
  bridge_compile "$(cygpath -m "$work/FactoryDispatch.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" \
    --type System.Delegate --type System.MulticastDelegate --type System.Action --type System.Threading.Thread \
    --type System.Windows.Threading.Dispatcher --type System.Windows.Threading.DispatcherOperation \
    --reference "$(cygpath -m "$core_ref/System.Threading.Thread.dll")" \
    --reference "$(cygpath -m "$build/lib/WindowsBase.dll")" -o "$(cygpath -m "$work/FactoryDispatch.dll")"
  cp "$here/wpf-tests/FactoryDispatch.cs" "$work/Program.cs"
  /usr/bin/python3 - "$work" "$build/lib" "$oob" <<'PYDISPATCHPROJECT'
from pathlib import Path
import subprocess, sys, xml.etree.ElementTree as E
work, lib, oob = map(Path, sys.argv[1:])
def win(p): return subprocess.check_output(['cygpath','-m',str(p.resolve())],text=True).strip()
root=E.Element('Project',Sdk='Microsoft.NET.Sdk'); pg=E.SubElement(root,'PropertyGroup')
for key,value in {'TargetFramework':'net8.0','OutputType':'Exe','UseAppHost':'false','EnableNETAnalyzers':'false','NuGetAudit':'false','UseSharedCompilation':'false'}.items(): E.SubElement(pg,key).text=value
items=E.SubElement(root,'ItemGroup')
for name,path in [('FactoryDispatch',work/'FactoryDispatch.dll'),('WindowsBase',lib/'WindowsBase.dll')]:
    item=E.SubElement(items,'Reference',Include=name); E.SubElement(item,'HintPath').text=win(path)
for name in ('System.Security.Permissions','System.IO.Packaging','System.Configuration.ConfigurationManager',
             'System.Security.Cryptography.Xml','System.Security.Cryptography.ProtectedData',
             'System.Diagnostics.EventLog','System.Windows.Extensions'):
    item=E.SubElement(items,'Reference',Include=name); E.SubElement(item,'HintPath').text=win(oob/(name+'.dll'))
E.ElementTree(root).write(work/'FactoryDispatchCheck.csproj',encoding='unicode')
PYDISPATCHPROJECT
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/FactoryDispatchCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -v:q
  cp "$build/lib/PresentationNative_cor3.dll" "$work/bin/Release/net8.0/"
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/FactoryDispatchCheck.dll")"
}

step_check_metrics_layout() {
  initialize
  local work=$build/metrics-layout-check
  local original=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  cat > "$work/MetricsLayout.cpp" <<EOF
#include <cstdint>
using UINT16 = std::uint16_t;
using INT16 = std::int16_t;
using UINT32 = std::uint32_t;
using INT32 = std::int32_t;
#include "$(cygpath -m "$original/FontMetrics.h")"
#include "$(cygpath -m "$original/GlyphMetrics.h")"
public ref class WpfMetricsIdentity abstract sealed {};
EOF
  bridge_compile "$(cygpath -m "$work/MetricsLayout.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" \
    --type System.Runtime.InteropServices.StructLayoutAttribute --type System.Runtime.InteropServices.LayoutKind \
    --type System.Runtime.InteropServices.FieldOffsetAttribute \
    --reference "$(cygpath -m "$core_ref/System.Runtime.InteropServices.dll")" -o "$(cygpath -m "$work/MetricsLayout.dll")"
  /clang64/bin/clang++.exe -std=c++14 -O2 -shared "$here/wpf-tests/MetricsNative.cpp" -o "$work/wpf-metrics-native.dll"
  cp "$here/wpf-tests/MetricsLayout.cs" "$work/Program.cs"
  cat > "$work/MetricsLayoutCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="MetricsLayout"><HintPath>MetricsLayout.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/MetricsLayoutCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$work/wpf-metrics-native.dll" "$work/bin/Release/net8.0/"
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/MetricsLayoutCheck.dll")"
}

step_check_foundations() {
  initialize
  local work=$build/foundation-check
  mkdir -p "$work"
  cp "$here/wpf-tests/Foundations.cs" "$work/Program.cs"
  /usr/bin/python3 - "$work" "$build/lib" "$oob" <<'PYPROJECT'
from pathlib import Path
import subprocess,sys,xml.etree.ElementTree as E
work,lib,oob=map(Path,sys.argv[1:])
def win(p): return subprocess.check_output(['cygpath','-m',str(p.resolve())],text=True).strip()
root=E.Element('Project',Sdk='Microsoft.NET.Sdk');pg=E.SubElement(root,'PropertyGroup')
for key,value in {'TargetFramework':'net8.0','OutputType':'Exe','UseAppHost':'false','EnableNETAnalyzers':'false','NuGetAudit':'false','UseSharedCompilation':'false'}.items(): E.SubElement(pg,key).text=value
items=E.SubElement(root,'ItemGroup')
for name in ('System.Windows.Input.Manipulations','System.Xaml','WindowsBase','UIAutomationTypes','UIAutomationProvider'):
    item=E.SubElement(items,'Reference',Include=name);E.SubElement(item,'HintPath').text=win(lib/(name+'.dll'))
for name in ('System.Security.Permissions','System.IO.Packaging','System.Configuration.ConfigurationManager',
             'System.Security.Cryptography.Xml','System.Security.Cryptography.ProtectedData',
             'System.Diagnostics.EventLog','System.Windows.Extensions'):
    item=E.SubElement(items,'Reference',Include=name);E.SubElement(item,'HintPath').text=win(oob/(name+'.dll'))
E.ElementTree(root).write(work/'FoundationCheck.csproj',encoding='utf-8')
PYPROJECT
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/FoundationCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -v:q
  cp "$build/lib/PresentationNative_cor3.dll" "$work/bin/Release/net8.0/"
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/FoundationCheck.dll")"
}

for step in $steps; do
  log "WPF $version: $step"
  "step_${step//-/_}"
done
