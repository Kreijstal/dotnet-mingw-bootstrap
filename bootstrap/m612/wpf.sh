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
    --type System.DateTime --type System.Threading.Monitor --type System.Security.SecurityCriticalAttribute \
    --type System.Uri --type System.IO.FileFormatException --type System.IntPtr --type System.Runtime.InteropServices.Marshal \
    --reference "$(cygpath -m "$core_ref/System.Threading.dll")" \
    --reference "$(cygpath -m "$core_ref/System.Runtime.InteropServices.dll")" \
    --reference "$(cygpath -m "$oob/System.IO.Packaging.dll")" \
    --reference "$(cygpath -m "$build/lib/WindowsBase.dll")" -I "$(cygpath -m "$shared")" \
    -I "$(cygpath -m "$cpp/..")" -I "$(cygpath -m "$cpp/TrueTypeSubsetter")" \
    -D '__field_bcount(x)=_Field_size_bytes_(x)' -D '__field_ecount(x)=_Field_size_(x)' \
    -D '__field_range(low,high)=_Field_range_(low,high)' -D '__deref_out_range(low,high)=_Deref_out_range_(low,high)' \
    -D '__success(x)=_Success_(x)' \
    -D "WINDOWS_BASE_DLL=\"$(cygpath -m "$build/lib/WindowsBase.dll")\"" \
    -D PRESENTATION_CORE=1 -D _CLR_NETCORE=1 -o "$(cygpath -m "$build/DirectWriteForwarder.il")" \
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
