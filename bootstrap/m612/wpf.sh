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
  CSC_DOTNET="$sdk/dotnet.exe" MSYSTEM=UCRT64 OOB='System.IO.Packaging System.Security.Permissions System.DirectoryServices' \
    bash "$here/libraries.sh" "$sdk/sdk/8.0.131/Roslyn/bincore/csc.dll" plan gens build report
  for name in System.IO.Packaging System.Security.Permissions System.DirectoryServices; do test -s "$oob/$name.dll"; done
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

step_build_presentation_tasks() {
  step_prepare_bridges
  build_assembly PresentationBuildTasks
}

step_check_presentation_tasks() {
  local work=$build/presentation-tasks-check
  mkdir -p "$work/tasks" "$work/obj"
  cp "$build/lib/PresentationBuildTasks.dll" "$work/tasks/"
  cp "$oob/System.CodeDom.dll" "$oob/System.Reflection.MetadataLoadContext.dll" "$work/tasks/"
  cp "$here/wpf-tests/MarkupView.xaml" "$work/"
  /usr/bin/python3 - "$work" "$build/lib" "$sdk" <<'PY'
from pathlib import Path
import subprocess,sys,xml.etree.ElementTree as E
work,lib,sdk=map(Path,sys.argv[1:])
def win(p): return subprocess.check_output(['cygpath','-m',str(p.resolve())],text=True).strip()
root=E.Element('Project',DefaultTargets='CompileMarkup')
E.SubElement(root,'UsingTask',TaskName='Microsoft.Build.Tasks.Windows.MarkupCompilePass1',AssemblyFile=win(work/'tasks/PresentationBuildTasks.dll'))
E.SubElement(root,'UsingTask',TaskName='Microsoft.Build.Tasks.Windows.ResourcesGenerator',AssemblyFile=win(work/'tasks/PresentationBuildTasks.dll'))
items=E.SubElement(root,'ItemGroup')
refs={path.stem:path for path in (sdk/'packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0').glob('*.dll')}
if not refs: raise SystemExit('Missing source-built net8.0 reference pack')
for name in ('WindowsBase','System.Xaml','PresentationCore','PresentationFramework','ReachFramework','DirectWriteForwarder','UIAutomationTypes','UIAutomationProvider'):
    refs[name]=lib/(name+'.dll')
for path in refs.values(): E.SubElement(items,'Reference',Include=win(path))
E.SubElement(items,'Page',Include=win(work/'MarkupView.xaml'))
target=E.SubElement(root,'Target',Name='CompileMarkup')
task=E.SubElement(target,'MarkupCompilePass1',Language='C#',LanguageSourceExtension='.cs',OutputPath=win(work/'obj')+'/',OutputType='Library',AssemblyName='PresentationFrameworkCheck',RootNamespace='Bootstrap',PageMarkup='@(Page)',References='@(Reference)',XamlDebuggingInformation='false',AlwaysCompileMarkupFilesInSeparateDomain='false')
for prop in ('GeneratedBamlFiles','GeneratedCodeFiles'):
    E.SubElement(task,'Output',TaskParameter=prop,ItemName=prop)
E.SubElement(target,'Error',Condition="'@(GeneratedBamlFiles)' == ''",Text='No BAML generated')
E.SubElement(target,'Error',Condition="'@(GeneratedCodeFiles)' == ''",Text='No C# generated')
E.SubElement(target,'ResourcesGenerator',ResourceFiles='@(GeneratedBamlFiles)',OutputPath=win(work/'obj')+'/',OutputResourcesFile=win(work/'obj/PresentationFrameworkCheck.g.resources'))
E.SubElement(target,'Message',Importance='high',Text='Source WPF markup compiler generated @(GeneratedBamlFiles) and @(GeneratedCodeFiles)')
E.ElementTree(root).write(work/'MarkupCheck.proj',encoding='utf-8')
PY
  "$sdk/dotnet.exe" msbuild "$(cygpath -m "$work/MarkupCheck.proj")" -nologo -v:minimal
  test -s "$work/obj/MarkupView.baml"
  test -s "$work/obj/MarkupView.g.cs"
  test -s "$work/obj/PresentationFrameworkCheck.g.resources"
}

step_native_win32() {
  step_prepare_bridges
  local work=$build/classification-native
  mkdir -p "$work"
  /usr/bin/python3 "$here/tools/wpf-classification-header.py" \
    "$source/src/Microsoft.DotNet.Wpf/src/PresentationCore/MS/internal/UnicodeClasses.cs" "$work/ClassificationEnums.h"
  /usr/bin/python3 "$here/tools/wpf-lineservices-header.py" \
    "$source/src/Microsoft.DotNet.Wpf/src/PresentationCore/MS/internal/TextFormatting/LineServices.cs" "$work/LineServicesNative.h"
  /clang64/bin/clang++.exe -O2 -shared "$here/wpf-native/PresentationNativeWin32.cpp" \
    "$here/wpf-native/Classification.cpp" "$here/wpf-native/TextAnalysis.cpp" "$here/wpf-native/LineServicesContext.cpp" -I "$work" \
    -o "$build/lib/PresentationNative_cor3.dll" -luser32 -lgdi32 -licuuc -licuin
}

step_inventory() {
  /usr/bin/python3 "$here/tools/wpfnative-inventory.py" "$source" "$build/lib/PresentationNative_cor3.dll" \
    "$build/native-coverage.json"
}

step_check_classification() {
  local native=$build/classification-native
  /clang64/bin/clang++.exe -std=c++17 -O2 "$here/wpf-tests/ClassificationNative.cpp" -I "$native" \
    -o "$native/check.exe" -licuuc
  "$native/check.exe" "$build/lib/PresentationNative_cor3.dll"
  local work=$build/classification-check
  mkdir -p "$work"
  cp "$here/wpf-tests/Classification.cs" "$work/Program.cs"
  /usr/bin/python3 - "$work" "$build/lib" <<'PY'
from pathlib import Path
import subprocess,sys,xml.etree.ElementTree as E
work,lib=map(Path,sys.argv[1:])
def win(p): return subprocess.check_output(['cygpath','-m',str(p.resolve())],text=True).strip()
root=E.Element('Project',Sdk='Microsoft.NET.Sdk'); pg=E.SubElement(root,'PropertyGroup')
for key,value in {'TargetFramework':'net8.0','OutputType':'Exe','UseAppHost':'false','EnableNETAnalyzers':'false','NuGetAudit':'false','UseSharedCompilation':'false'}.items(): E.SubElement(pg,key).text=value
items=E.SubElement(root,'ItemGroup')
for name in ('PresentationCore','WindowsBase','System.Xaml','DirectWriteForwarder'):
    item=E.SubElement(items,'Reference',Include=name); E.SubElement(item,'HintPath').text=win(lib/(name+'.dll'))
E.ElementTree(root).write(work/'ClassificationCheck.csproj',encoding='utf-8')
PY
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/ClassificationCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -v:q
  cp "$build/lib/PresentationNative_cor3.dll" "$build/lib/wpf-dwrite-loader.dll" "$build/lib/wpf-subsetter.dll" "$work/bin/Release/net8.0/"
  "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/ClassificationCheck.dll")"
}

step_check_text_analysis_native() {
  local work=$build/text-analysis-native-check
  mkdir -p "$work"
  /clang64/bin/clang++.exe -std=c++17 -O2 "$here/wpf-tests/TextAnalysisNative.cpp" -o "$work/check.exe" -ldwrite
  "$work/check.exe" "$build/lib/PresentationNative_cor3.dll"
}

step_check_text_analysis() {
  initialize
  local work=$build/text-analysis-check
  mkdir -p "$work"
  cp "$here/wpf-tests/TextAnalysis.cs" "$work/Program.cs"
  /usr/bin/python3 - "$work" "$build/lib" "$oob" <<'PY'
from pathlib import Path
import subprocess,sys,xml.etree.ElementTree as E
work,lib,oob=map(Path,sys.argv[1:])
def win(p): return subprocess.check_output(['cygpath','-m',str(p.resolve())],text=True).strip()
root=E.Element('Project',Sdk='Microsoft.NET.Sdk'); pg=E.SubElement(root,'PropertyGroup')
for key,value in {'TargetFramework':'net8.0','OutputType':'Exe','UseAppHost':'false','AllowUnsafeBlocks':'true','EnableNETAnalyzers':'false','NuGetAudit':'false','UseSharedCompilation':'false'}.items(): E.SubElement(pg,key).text=value
items=E.SubElement(root,'ItemGroup')
for name in ('PresentationCore','WindowsBase','System.Xaml','DirectWriteForwarder'):
    item=E.SubElement(items,'Reference',Include=name); E.SubElement(item,'HintPath').text=win(lib/(name+'.dll'))
for file in sorted(oob.glob('*.dll')):
    item=E.SubElement(items,'Reference',Include=file.stem); E.SubElement(item,'HintPath').text=win(file)
E.ElementTree(root).write(work/'TextAnalysisCheck.csproj',encoding='utf-8')
PY
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/TextAnalysisCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -v:q
  cp "$build/lib/PresentationNative_cor3.dll" "$build/lib/wpf-dwrite-loader.dll" "$build/lib/wpf-subsetter.dll" "$work/bin/Release/net8.0/"
  "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/TextAnalysisCheck.dll")"
}

step_check_lineservices_context() {
  initialize
  local work=$build/lineservices-context-check
  mkdir -p "$work"
  /clang64/bin/clang++.exe -std=c++17 -O2 "$here/wpf-tests/LineServicesContextNative.cpp" \
    -I "$build/classification-native" -o "$work/native-check.exe"
  "$work/native-check.exe" "$build/lib/PresentationNative_cor3.dll"
  cp "$here/wpf-tests/LineServicesContext.cs" "$work/Program.cs"
  /usr/bin/python3 - "$work" "$build/lib" "$oob" <<'PY'
from pathlib import Path
import subprocess,sys,xml.etree.ElementTree as E
work,lib,oob=map(Path,sys.argv[1:])
def win(p): return subprocess.check_output(['cygpath','-m',str(p.resolve())],text=True).strip()
root=E.Element('Project',Sdk='Microsoft.NET.Sdk'); pg=E.SubElement(root,'PropertyGroup')
for key,value in {'TargetFramework':'net8.0','OutputType':'Exe','UseAppHost':'false','EnableNETAnalyzers':'false','NuGetAudit':'false','UseSharedCompilation':'false'}.items(): E.SubElement(pg,key).text=value
items=E.SubElement(root,'ItemGroup')
for name in ('PresentationCore','WindowsBase','System.Xaml','DirectWriteForwarder'):
    item=E.SubElement(items,'Reference',Include=name); E.SubElement(item,'HintPath').text=win(lib/(name+'.dll'))
for file in sorted(oob.glob('*.dll')):
    item=E.SubElement(items,'Reference',Include=file.stem); E.SubElement(item,'HintPath').text=win(file)
E.ElementTree(root).write(work/'ContextCheck.csproj',encoding='utf-8')
PY
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/ContextCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -v:q
  cp "$build/lib/PresentationNative_cor3.dll" "$build/lib/wpf-dwrite-loader.dll" "$build/lib/wpf-subsetter.dll" "$work/bin/Release/net8.0/"
  "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/ContextCheck.dll")"
}

step_check_lineservices_line_native() {
  local work=$build/lineservices-line-check
  mkdir -p "$work"
  /clang64/bin/clang++.exe -std=c++17 -O2 "$here/wpf-tests/LineServicesLineNative.cpp" \
    -I "$build/classification-native" -o "$work/native-check.exe"
  "$work/native-check.exe" "$build/lib/PresentationNative_cor3.dll"
}

step_check_text_line() {
  initialize
  local work=$build/text-line-check
  mkdir -p "$work"
  cp "$here/wpf-tests/TextLine.cs" "$work/Program.cs"
  /usr/bin/python3 - "$work" "$build/lib" "$oob" <<'PY'
from pathlib import Path
import subprocess,sys,xml.etree.ElementTree as E
work,lib,oob=map(Path,sys.argv[1:])
def win(p): return subprocess.check_output(['cygpath','-m',str(p.resolve())],text=True).strip()
root=E.Element('Project',Sdk='Microsoft.NET.Sdk'); pg=E.SubElement(root,'PropertyGroup')
for key,value in {'TargetFramework':'net8.0','OutputType':'Exe','UseAppHost':'false','EnableNETAnalyzers':'false','NuGetAudit':'false','UseSharedCompilation':'false'}.items(): E.SubElement(pg,key).text=value
items=E.SubElement(root,'ItemGroup')
for name in ('PresentationCore','WindowsBase','System.Xaml','DirectWriteForwarder'):
    item=E.SubElement(items,'Reference',Include=name); E.SubElement(item,'HintPath').text=win(lib/(name+'.dll'))
for file in sorted(oob.glob('*.dll')):
    item=E.SubElement(items,'Reference',Include=file.stem); E.SubElement(item,'HintPath').text=win(file)
E.ElementTree(root).write(work/'TextLineCheck.csproj',encoding='utf-8')
PY
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/TextLineCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -v:q
  cp "$build/lib/PresentationNative_cor3.dll" "$build/lib/wpf-dwrite-loader.dll" "$build/lib/wpf-subsetter.dll" "$build/lib/wpfgfx_cor3.dll" "$work/bin/Release/net8.0/"
  "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/TextLineCheck.dll")"
}

step_prepare_gfx() {
  local root=$build/gfx-source
  local gfx=$root/src/Microsoft.DotNet.Wpf/src/WpfGfx
  local port=$here/wpf-native/gfx
  if [ ! -d "$gfx" ]; then
    mkdir -p "$(dirname "$gfx")"
    cp -r "$source/src/Microsoft.DotNet.Wpf/src/WpfGfx" "$source/src/Microsoft.DotNet.Wpf/src/Shared" "$(dirname "$gfx")/"
  fi
  local patchfile
  for patchfile in "$port"/000*.patch; do
    if ! patch --batch --force --dry-run -R -p1 -d "$root" -i "$patchfile" >/dev/null 2>&1; then
      patch --batch --forward -p1 -d "$root" -i "$patchfile"
    fi
  done
  [ -f "$gfx/core/hw/Shaders.rc.upstream" ] || mv "$gfx/core/hw/Shaders.rc" "$gfx/core/hw/Shaders.rc.upstream"
  cmp -s "$port/CMakeLists.txt" "$gfx/CMakeLists.txt" || cp "$port/CMakeLists.txt" "$gfx/"
  mkdir -p "$gfx/mingw/codeanalysis"
  local input
  for input in NativeVersion.rc wpf_mingw_compat.h wpfgfx_mingw_compat.h winnt.h wpp_stub.h dllcrt_alias.cpp wpfgfx_mingw_shims.cpp fxasm.py gendef.py; do
    cmp -s "$port/$input" "$gfx/mingw/$input" || cp "$port/$input" "$gfx/mingw/"
  done
  cmp -s "$port/sourceannotations.h" "$gfx/mingw/codeanalysis/sourceannotations.h" || cp "$port/sourceannotations.h" "$gfx/mingw/codeanalysis/"
}

step_build_gfx() {
  step_prepare_gfx
  local gfx=$build/gfx-source/src/Microsoft.DotNet.Wpf/src/WpfGfx
  CC=/clang64/bin/clang.exe CXX=/clang64/bin/clang++.exe \
    /clang64/bin/cmake.exe -S "$gfx" -B "$build/gfx-build" -GNinja \
      -DCMAKE_BUILD_TYPE=Release -DWPF_VERSION="$version"
  /clang64/bin/cmake.exe --build "$build/gfx-build" --parallel "${WPF_JOBS:-4}"
  cp "$build/gfx-build/wpfgfx_cor3.dll" "$build/lib/"
}

step_check_gfx() {
  local gfx=$build/gfx-source/src/Microsoft.DotNet.Wpf/src/WpfGfx
  local port=$here/wpf-native/gfx
  /clang64/bin/python.exe "$port/fxasm.py" --check "$gfx/core/hw/Shaders.rc.upstream" \
    Text11A="$gfx/core/hw/shaders/hlslTextShaders11A.fx" \
    Text11L="$gfx/core/hw/shaders/hlslTextShaders11L.fx" \
    Text20A="$gfx/core/hw/shaders/hlslTextShaders20A.fx" \
    Text20L="$gfx/core/hw/shaders/hlslTextShaders20L.fx"
  /clang64/bin/clang.exe -O2 "$port/exporttest.c" -o "$build/gfx-exporttest.exe"
  "$build/gfx-exporttest.exe" "$build/lib/wpfgfx_cor3.dll" "$build/gfx-build/gen/wpfgfx.def"
  /clang64/bin/clang.exe -O2 "$port/gfxtest.c" -I "$gfx/include" -o "$build/gfx-test.exe"
  "$build/gfx-test.exe" "$build/lib/wpfgfx_cor3.dll"
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
    if ! patch --batch --force --dry-run -R -p1 -d "$source" -i "$patchfile" >/dev/null 2>&1; then
      patch --batch --forward -p1 -d "$source" -i "$patchfile"
    fi
  done
}

step_check_checked_integers() {
  step_prepare_bridges
  local inc=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/TrueTypeSubsetter/TtfDelta
  /clang64/bin/clang++.exe -std=c++14 -O2 "$here/wpf-tests/CheckedIntegers.cpp" -I "$inc" -o "$build/checked-integers.exe"
  "$build/checked-integers.exe"
}

step_native_subsetter() {
  step_prepare_bridges
  local subset=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/TrueTypeSubsetter
  /clang64/bin/clang++.exe -std=c++14 -fms-extensions -ftrivial-auto-var-init=zero -O2 -shared -DNDEBUG -DINTEL \
    "$here/wpf-native/SubsetterBridge.cpp" -I "$subset/TtfDelta" -I "$subset" \
    -D '__checkReturn=_Check_return_' -D '__success(x)=_Success_(x)' \
    -D '__in_bcount(x)=_In_reads_bytes_(x)' -D '__out_bcount(x)=_Out_writes_bytes_(x)' \
    -D '__inout_bcount(x)=_Inout_updates_bytes_(x)' \
    -D '__field_bcount(x)=_Field_size_bytes_(x)' -D '__field_ecount(x)=_Field_size_(x)' \
    -D '__in_ecount(x)=_In_reads_(x)' -D '__out_ecount(x)=_Out_writes_(x)' \
    -D '__field_range(low,high)=_Field_range_(low,high)' \
    -o "$build/lib/wpf-subsetter.dll" "-Wl,--out-implib,$build/lib/libwpf-subsetter.a"
}

step_bridge_gate() {
  step_prepare_bridges
  [ -s "$build/lib/libwpf-subsetter.a" ] || step_native_subsetter
  [ -s "$build/lib/libwpf-dwrite-loader.a" ] || step_native_dwrite_loader
  local cpp=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP
  local shared=$source/src/Microsoft.DotNet.Wpf/src/Shared/inc
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  # Include the actual startup unit so its factory lookup is an emitted
  # definition in the same CLR module, rather than an unresolved C++ extern.
  # Replay the version fields from Wpf.Cpp.targets' generated AssemblyInfo.
  cat > "$build/DirectWriteAssemblyInfo.cpp" <<EOF
using namespace System::Reflection;
[assembly:AssemblyVersion("$version.0")];
[assembly:AssemblyFileVersion("$version.0")];
[assembly:AssemblyInformationalVersion("$version")];
EOF
  cat > "$build/DirectWriteForwarderCombined.cpp" <<EOF
#include "$(cygpath -m "$build/DirectWriteAssemblyInfo.cpp")"
#include "$(cygpath -m "$cpp/DirectWriteForwarder.cpp")"
#include "$(cygpath -m "$cpp/../main.cpp")"
#include "$(cygpath -m "$cpp/../OtherAssemblyAttrs.cpp")"
EOF
  bridge_compile "$(cygpath -m "$build/DirectWriteForwarderCombined.cpp")" --std c++14 --target library --emit il \
    --native-import "$(cygpath -m /clang64/lib/libadvapi32.a)" \
    --native-import "$(cygpath -m /clang64/lib/libkernel32.a)" \
    --native-import "$(cygpath -m "$build/lib/libwpf-subsetter.a")" \
    --native-import "$(cygpath -m "$build/lib/libwpf-dwrite-loader.a")" \
    -D WPF_SUBSETTER_SPLIT=1 -D WPF_MANAGED_STARTUP=1 -I "$(cygpath -m "$here/wpf-native")" \
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
    --type System.AppDomain --type System.EventHandler --type System.EventArgs --type System.DllNotFoundException \
    --type System.ComponentModel.Win32Exception --type System.Runtime.InteropServices.ExternalException --type System.SystemException \
    --type System.Reflection.Assembly --type System.Runtime.CompilerServices.DependencyAttribute --type System.Runtime.CompilerServices.LoadHint \
    --type System.Runtime.CompilerServices.InternalsVisibleToAttribute \
    --type System.Reflection.AssemblyFileVersionAttribute --type System.Reflection.AssemblyInformationalVersionAttribute \
    --type System.Windows.Threading.Dispatcher --type System.Windows.Threading.DispatcherOperation \
    --type System.Action --type System.Math --type System.Runtime.InteropServices.GCHandle --type System.Runtime.InteropServices.GCHandleType \
    --reference "$(cygpath -m "$core_ref/System.Threading.dll")" \
    --reference "$(cygpath -m "$core_ref/System.Threading.Thread.dll")" \
    --reference "$(cygpath -m "$core_ref/System.Collections.dll")" \
    --reference "$(cygpath -m "$core_ref/System.Runtime.InteropServices.dll")" \
    --reference "$(cygpath -m "$core_ref/System.Net.Requests.dll")" \
    --reference "$(cygpath -m "$core_ref/System.Globalization.dll")" \
    --reference "$(cygpath -m "$core_ref/Microsoft.Win32.Primitives.dll")" \
    --reference "$(cygpath -m "$oob/System.IO.Packaging.dll")" \
    --reference "$(cygpath -m "$build/lib/WindowsBase.dll")" -I "$(cygpath -m "$shared")" \
    -I "$(cygpath -m "$cpp")" -I "$(cygpath -m "$cpp/..")" -I "$(cygpath -m "$cpp/TrueTypeSubsetter")" \
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

step_build_directwrite() {
  step_bridge_gate
  /clang64/bin/ilasm-dotnet.exe "$(cygpath -m "$build/DirectWriteForwarder.il")" -dll -quiet -nologo \
    "-output=$(cygpath -m "$build/lib/DirectWriteForwarder.dll")"
}

step_printing_gate() {
  initialize
  /usr/bin/python3 "$here/tools/wpf-printing-gate.py" "$source" "$build" "$sdk" "$oob" \
    "${WPF_CPPCLI_BUILD:-$M612/build/cppcli}" > "$build/printing-gate.log" 2>&1 || {
      tail -100 "$build/printing-gate.log"
      die "The source frontend cannot compile the original System.Printing project yet; see printing-gate.log"
    }
}

step_check_directwrite_assembly() {
  initialize
  [ -s "$build/lib/DirectWriteForwarder.dll" ] || step_build_directwrite
  local work=$build/directwrite-assembly-check
  local peer=$build/directwrite-friend-check
  mkdir -p "$work"
  mkdir -p "$peer"
  cp "$here/wpf-tests/DirectWriteFriend.cs" "$peer/Probe.cs"
  cat > "$peer/Friend.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><AssemblyName>PresentationCore</AssemblyName><PublicSign>true</PublicSign><SignAssembly>true</SignAssembly><AssemblyOriginatorKeyFile>../keys/MSShared.snk</AssemblyOriginatorKeyFile><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="DirectWriteForwarder"><HintPath>../lib/DirectWriteForwarder.dll</HintPath></Reference><Reference Include="WindowsBase"><HintPath>../lib/WindowsBase.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$peer/Friend.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$here/wpf-tests/DirectWriteAssembly.cs" "$work/Program.cs"
  cat > "$work/DirectWriteAssemblyCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="DirectWriteForwarder"><HintPath>../lib/DirectWriteForwarder.dll</HintPath></Reference><Reference Include="WindowsBase"><HintPath>../lib/WindowsBase.dll</HintPath></Reference><Reference Include="System.IO.Packaging"><HintPath>System.IO.Packaging.dll</HintPath></Reference><Reference Include="PresentationCore"><HintPath>../directwrite-friend-check/bin/Release/net8.0/PresentationCore.dll</HintPath></Reference></ItemGroup></Project>
EOF
  cp "$oob/System.IO.Packaging.dll" "$work/System.IO.Packaging.dll"
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/DirectWriteAssemblyCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$build/lib/wpf-dwrite-loader.dll" "$build/lib/wpf-subsetter.dll" "$work/bin/Release/net8.0/"
  timeout 60 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/DirectWriteAssemblyCheck.dll")"
}

step_build_presentation_core() {
  [ -s "$build/lib/DirectWriteForwarder.dll" ] || step_build_directwrite
  build_assembly PresentationCore
}

step_check_presentation_core() {
  initialize
  local work=$build/presentation-core-check
  mkdir -p "$work"
  cp "$here/wpf-tests/PresentationCore.cs" "$work/Program.cs"
  /usr/bin/python3 - "$work" "$build/lib" "$oob" <<'PY'
from pathlib import Path
import subprocess,sys,xml.etree.ElementTree as E
work,lib,oob=map(Path,sys.argv[1:])
def win(p): return subprocess.check_output(['cygpath','-m',str(p.resolve())],text=True).strip()
root=E.Element('Project',Sdk='Microsoft.NET.Sdk');pg=E.SubElement(root,'PropertyGroup')
for key,value in {'TargetFramework':'net8.0','OutputType':'Exe','UseAppHost':'false','EnableNETAnalyzers':'false','NuGetAudit':'false','UseSharedCompilation':'false'}.items(): E.SubElement(pg,key).text=value
items=E.SubElement(root,'ItemGroup')
for name in ('PresentationCore','DirectWriteForwarder','WindowsBase','System.Xaml','System.Windows.Input.Manipulations','UIAutomationTypes','UIAutomationProvider'):
    item=E.SubElement(items,'Reference',Include=name);E.SubElement(item,'HintPath').text=win(lib/(name+'.dll'))
for file in sorted(oob.glob('*.dll')):
    item=E.SubElement(items,'Reference',Include=file.stem);E.SubElement(item,'HintPath').text=win(file)
E.ElementTree(root).write(work/'PresentationCoreCheck.csproj',encoding='utf-8')
PY
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/PresentationCoreCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$build/lib/PresentationNative_cor3.dll" "$build/lib/wpf-dwrite-loader.dll" "$build/lib/wpf-subsetter.dll" "$work/bin/Release/net8.0/"
  timeout 60 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/PresentationCoreCheck.dll")"
}

step_build_reach_framework() {
  initialize
  /usr/bin/python3 "$here/tools/wpfgraph.py" "$source" "$build" "$sdk" "$oob" "$build/keys" ReachFramework/ReachFramework.csproj
}

step_build_presentation_framework() {
  initialize
  /usr/bin/python3 "$here/tools/wpfgraph.py" "$source" "$build" "$sdk" "$oob" "$build/keys" PresentationFramework/PresentationFramework.csproj
}

step_build_themes() {
  initialize
  for name in Aero2 Aero AeroLite Classic Luna Royale; do
    /usr/bin/python3 "$here/tools/wpfgraph.py" "$source" "$build" "$sdk" "$oob" "$build/keys" "Themes/PresentationFramework.$name/PresentationFramework.$name.csproj"
  done
}

step_build_presentation_ui() {
  initialize
  /usr/bin/python3 "$here/tools/wpfgraph.py" "$source" "$build" "$sdk" "$oob" "$build/keys" PresentationUI/PresentationUI.csproj
}

step_build_penimc() {
  bash "$here/wpf-penimc.sh" build check
}

step_check_window() {
  initialize
  local work=$build/window-check
  mkdir -p "$work"
  cp "$here/wpf-tests/Window.cs" "$work/Program.cs"
  /usr/bin/python3 - "$work" "$build/lib" "$oob" <<'PY'
from pathlib import Path
import subprocess,sys,xml.etree.ElementTree as E
work,lib,oob=map(Path,sys.argv[1:])
def win(p): return subprocess.check_output(['cygpath','-m',str(p.resolve())],text=True).strip()
root=E.Element('Project',Sdk='Microsoft.NET.Sdk'); pg=E.SubElement(root,'PropertyGroup')
for key,value in {'TargetFramework':'net8.0','OutputType':'Exe','UseAppHost':'false','EnableNETAnalyzers':'false','NuGetAudit':'false','UseSharedCompilation':'false'}.items(): E.SubElement(pg,key).text=value
items=E.SubElement(root,'ItemGroup')
for name in ('PresentationFramework','PresentationCore','ReachFramework','WindowsBase','System.Xaml','DirectWriteForwarder','PresentationUI','UIAutomationTypes','UIAutomationProvider','System.Windows.Input.Manipulations',
             'PresentationFramework.Aero','PresentationFramework.Aero2','PresentationFramework.AeroLite','PresentationFramework.Classic','PresentationFramework.Luna','PresentationFramework.Royale'):
    item=E.SubElement(items,'Reference',Include=name); E.SubElement(item,'HintPath').text=win(lib/(name+'.dll'))
for file in sorted(oob.glob('*.dll')):
    item=E.SubElement(items,'Reference',Include=file.stem); E.SubElement(item,'HintPath').text=win(file)
E.ElementTree(root).write(work/'WindowCheck.csproj',encoding='utf-8')
PY
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/WindowCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -v:q
  cp "$build/lib/PresentationNative_cor3.dll" "$build/lib/wpfgfx_cor3.dll" "$build/lib/PenImc_cor3.dll" \
    "$build/lib/wpf-dwrite-loader.dll" "$build/lib/wpf-subsetter.dll" "$build/lib/PresentationFramework."*.dll "$work/bin/Release/net8.0/"
  /c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -NonInteractive \
    -File "$(cygpath -m "$here/wpf-tests/RunInteractive.ps1")" \
    -Work "$(cygpath -m "$work/bin/Release/net8.0")" -Dotnet "$(cygpath -m "$sdk/dotnet.exe")" -ToolchainBin "$(cygpath -m /clang64/bin)"
}

step_check_window_text() {
  local work=$build/window-check
  /c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -NonInteractive \
    -File "$(cygpath -m "$here/wpf-tests/RunInteractive.ps1")" \
    -Work "$(cygpath -m "$work/bin/Release/net8.0")" -Dotnet "$(cygpath -m "$sdk/dotnet.exe")" -ToolchainBin "$(cygpath -m /clang64/bin)" -Text
}

step_check_window_indic() {
  local work=$build/window-check
  /c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -NonInteractive \
    -File "$(cygpath -m "$here/wpf-tests/RunInteractive.ps1")" \
    -Work "$(cygpath -m "$work/bin/Release/net8.0")" -Dotnet "$(cygpath -m "$sdk/dotnet.exe")" -ToolchainBin "$(cygpath -m /clang64/bin)" -Text -Indic
}
step_check_window_wrap() {
  local work=$build/window-check
  for variant in Latin Indic; do
    local extra=()
    [ "$variant" != Indic ] || extra=(-Indic)
    /c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -NonInteractive \
      -File "$(cygpath -m "$here/wpf-tests/RunInteractive.ps1")" \
      -Work "$(cygpath -m "$work/bin/Release/net8.0")" -Dotnet "$(cygpath -m "$sdk/dotnet.exe")" \
      -ToolchainBin "$(cygpath -m /clang64/bin)" -Text -Wrap "${extra[@]}"
  done
}
step_check_window_edit() {
  local work=$build/window-check
  /c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -NonInteractive \
    -File "$(cygpath -m "$here/wpf-tests/RunInteractive.ps1")" \
    -Work "$(cygpath -m "$work/bin/Release/net8.0")" -Dotnet "$(cygpath -m "$sdk/dotnet.exe")" \
    -ToolchainBin "$(cygpath -m /clang64/bin)" -Text -Edit
}
step_check_presentation_framework() {
  initialize
  local work=$build/presentation-framework-check
  mkdir -p "$work"
  cp "$here/wpf-tests/PresentationFramework.cs" "$work/Program.cs"
  /usr/bin/python3 - "$work" "$build/lib" "$oob" "$build/presentation-tasks-check/obj" <<'PY'
from pathlib import Path
import subprocess,sys,xml.etree.ElementTree as E
work,lib,oob,markup=map(Path,sys.argv[1:])
def win(p): return subprocess.check_output(['cygpath','-m',str(p.resolve())],text=True).strip()
root=E.Element('Project',Sdk='Microsoft.NET.Sdk');pg=E.SubElement(root,'PropertyGroup')
for key,value in {'TargetFramework':'net8.0','OutputType':'Exe','UseAppHost':'false','EnableNETAnalyzers':'false','NuGetAudit':'false','UseSharedCompilation':'false'}.items(): E.SubElement(pg,key).text=value
items=E.SubElement(root,'ItemGroup')
for name in ('PresentationFramework','ReachFramework','PresentationCore','DirectWriteForwarder','WindowsBase','System.Xaml','System.Windows.Input.Manipulations','UIAutomationTypes','UIAutomationProvider'):
    item=E.SubElement(items,'Reference',Include=name);E.SubElement(item,'HintPath').text=win(lib/(name+'.dll'))
for file in sorted(oob.glob('*.dll')):
    item=E.SubElement(items,'Reference',Include=file.stem);E.SubElement(item,'HintPath').text=win(file)
for file in (markup/'MarkupView.g.cs',markup/'PresentationFrameworkCheck.g.resources'):
    if not file.is_file(): raise SystemExit('Run check-presentation-tasks first: '+str(file))
E.SubElement(items,'Compile',Include=win(markup/'MarkupView.g.cs'))
item=E.SubElement(items,'EmbeddedResource',Include=win(markup/'PresentationFrameworkCheck.g.resources'))
E.SubElement(item,'LogicalName').text='PresentationFrameworkCheck.g.resources'
E.ElementTree(root).write(work/'PresentationFrameworkCheck.csproj',encoding='utf-8')
PY
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/PresentationFrameworkCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$build/lib/PresentationNative_cor3.dll" "$build/lib/wpf-dwrite-loader.dll" "$build/lib/wpf-subsetter.dll" "$work/bin/Release/net8.0/"
  cp "$build/lib/wpfgfx_cor3.dll" "$work/bin/Release/net8.0/"
  timeout 60 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/PresentationFrameworkCheck.dll")"
}

step_native_dwrite_loader() {
  step_prepare_bridges
  local shared=$source/src/Microsoft.DotNet.Wpf/src/Shared
  /clang64/bin/clang++.exe -std=c++14 -fms-extensions -O2 -shared "$here/wpf-native/DWriteLoaderBridge.cpp" \
    -I "$shared/cpp" -I "$shared/inc" -D UNICODE=1 -D _UNICODE=1 \
    -o "$build/lib/wpf-dwrite-loader.dll" "-Wl,--out-implib,$build/lib/libwpf-dwrite-loader.a"
}

step_check_dwrite_startup() {
  initialize
  step_native_dwrite_loader
  local work=$build/dwrite-startup-check
  local original=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder
  local shared=$source/src/Microsoft.DotNet.Wpf/src/Shared/inc
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work/CPP"
  cp "$original/main.cpp" "$work/main.cpp"
  printf '#include <windows.h>\n#include <vcclr.h>\n' > "$work/CPP/precomp.hxx"
  cat > "$work/StartupCheck.cpp" <<'EOF'
#include "main.cpp"
#include <dwrite.h>
using FactoryFunction = HRESULT (WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
public ref class WpfStartupCheck abstract sealed {
public:
static System::IntPtr Address() { return System::IntPtr(GetDWriteCreateFactoryFunctionPointer()); }
static void Reload() { MS::Internal::NativeWPFDLLLoader::LoadDwrite(); }
static bool CreateAndRelease() {
    FactoryFunction create = reinterpret_cast<FactoryFunction>(GetDWriteCreateFactoryFunctionPointer());
    IUnknown* factory = nullptr;
    if (!create || create(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),&factory) != S_OK || !factory) return false;
    factory->Release();
    return true;
}
};
EOF
  bridge_compile "$(cygpath -m "$work/StartupCheck.cpp")" --std c++14 --target library \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" \
    --type System.AppDomain --type System.EventHandler --type System.EventArgs --type System.Exception \
    --type System.DllNotFoundException --type System.InvalidOperationException --type System.ComponentModel.Win32Exception \
    --type System.Runtime.InteropServices.Marshal --type System.Runtime.InteropServices.ExternalException --type System.SystemException \
    --type System.Reflection.Assembly --type System.Runtime.CompilerServices.DependencyAttribute --type System.Runtime.CompilerServices.LoadHint \
    --type System.Security.SecurityCriticalAttribute --type System.Diagnostics.Debug \
    --reference "$(cygpath -m "$core_ref/Microsoft.Win32.Primitives.dll")" \
    --native-import "$(cygpath -m "$build/lib/libwpf-dwrite-loader.a")" \
    --native-import "$(cygpath -m /clang64/lib/libkernel32.a)" \
    -D WPF_MANAGED_STARTUP=1 -D WPF_SUBSETTER_SPLIT=1 \
    -I "$(cygpath -m "$here/wpf-native")" -I "$(cygpath -m "$shared")" -o "$(cygpath -m "$work/StartupCheck.dll")"
  cp "$here/wpf-tests/DWriteStartup.cs" "$work/Program.cs"
  cat > "$work/StartupLifecycleCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="StartupCheck"><HintPath>StartupCheck.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/StartupLifecycleCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$build/lib/wpf-dwrite-loader.dll" "$work/bin/Release/net8.0/"
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/StartupLifecycleCheck.dll")" > "$work/runtime.log" 2>&1
  cat "$work/runtime.log"
  grep -q 'actual WPF ProcessExit cleanup OK' "$work/runtime.log"
}

step_check_function_pointer() {
  step_native_dwrite_loader
  local work=$build/function-pointer-check
  mkdir -p "$work"
  cp "$build/lib/wpf-dwrite-loader.dll" "$work/"
  local mode
  for mode in 14 17; do
    bridge_compile "$(cygpath -m "$here/wpf-tests/FunctionPointerDWrite.cpp")" --std "c++$mode" \
      --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --type System.GC \
      --native-library wpf-dwrite-loader.dll -o "$(cygpath -m "$work/FunctionPointerDWrite$mode.dll")"
    timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/FunctionPointerDWrite$mode.dll")"
  done
}

step_check_native_cleanup() {
  initialize
  local work=$build/native-cleanup-check
  local original=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  cp "$original/NativePointerWrapper.cpp" "$original/NativePointerWrapper.h" "$work/"
  printf '#include <windows.h>\n#include <vcclr.h>\nusing namespace System;\n' > "$work/Common.h"
  cat > "$work/NativeCleanup.cpp" <<'EOF'
#include "NativePointerWrapper.cpp"
using namespace MS::Internal::Text::TextInterface::Generics;
public ref class WpfNativeCleanup abstract sealed {
public:
    static System::Runtime::InteropServices::CriticalHandle^ Com(System::IntPtr pointer) {
        return gcnew NativeIUnknownWrapper<IUnknown>(static_cast<IUnknown*>(pointer.ToPointer()));
    }
    static System::Runtime::InteropServices::CriticalHandle^ Pod() {
        return gcnew NativePointerWrapper<_GUID>(new _GUID());
    }
    static System::Runtime::InteropServices::CriticalHandle^ Empty() {
        return gcnew NativePointerWrapper<_GUID>(static_cast<_GUID*>(nullptr));
    }
};
EOF
  bridge_compile "$(cygpath -m "$work/NativeCleanup.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" \
    --type System.Runtime.InteropServices.CriticalHandle --type System.Runtime.ConstrainedExecution.CriticalFinalizerObject \
    --type System.Runtime.ConstrainedExecution.Cer --type System.Runtime.ConstrainedExecution.Consistency \
    --type System.Runtime.ConstrainedExecution.ReliabilityContractAttribute \
    --reference "$(cygpath -m "$core_ref/System.Runtime.InteropServices.dll")" -o "$(cygpath -m "$work/NativeCleanup.dll")"
  cmp "$original/NativePointerWrapper.cpp" "$work/NativePointerWrapper.cpp"
  cmp "$original/NativePointerWrapper.h" "$work/NativePointerWrapper.h"
  /clang64/bin/clang++.exe -std=c++17 -shared -O2 "$here/cppcli/tests/com-native.cpp" -luuid -o "$work/native-cleanup-probe.dll"
  cp "$here/wpf-tests/NativeCleanup.cs" "$work/Program.cs"
  cat > "$work/NativeCleanupCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="NativeCleanup"><HintPath>NativeCleanup.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/NativeCleanupCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$work/native-cleanup-probe.dll" "$work/bin/Release/net8.0/"
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/NativeCleanupCheck.dll")"
}

step_check_font_file_zero() {
  initialize
  local work=$build/font-file-zero-check
  local original=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  cp "$original/NativePointerWrapper.cpp" "$original/NativePointerWrapper.h" "$original/OpenTypeTableTag.h" "$work/"
  printf '#include <windows.h>\n#include <dwrite.h>\n#include <vcclr.h>\nusing namespace System;\ntypedef unsigned char byte;\n' > "$work/Common.h"
  python3 - "$original/FontFace.cpp" "$work/FontFileZeroOriginal.inc" <<'PY'
import pathlib, sys
source = pathlib.Path(sys.argv[1]).read_bytes()
start = source.index(b'    __declspec(noinline) FontFile^ FontFace::GetFileZero()')
end = source.index(b'    __declspec(noinline) unsigned int FontFace::Index::get()', start)
pathlib.Path(sys.argv[2]).write_bytes(source[start:end])
assert pathlib.Path(sys.argv[2]).read_bytes() == source[start:end]
start = source.index(b'    void FontFace::GetArrayOfGlyphIndices(')
end = source.index(b'}}}}//MS::Internal::Text::TextInterface', start)
destination = pathlib.Path(sys.argv[2]).with_name('FontFaceTablesOriginal.inc')
destination.write_bytes(source[start:end])
assert destination.read_bytes() == source[start:end]
PY
  cp "$here/wpf-tests/FontFileZero.cpp" "$work/"
  bridge_compile "$(cygpath -m "$work/FontFileZero.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --native-library font-file-probe.dll \
    --type System.Runtime.InteropServices.CriticalHandle --type System.Runtime.InteropServices.Marshal \
    --type System.Runtime.ConstrainedExecution.CriticalFinalizerObject \
    --type System.Runtime.ConstrainedExecution.Cer --type System.Runtime.ConstrainedExecution.Consistency \
    --type System.Runtime.ConstrainedExecution.ReliabilityContractAttribute --type System.GC \
    --type System.InvalidOperationException --type System.Exception \
    --reference "$(cygpath -m "$core_ref/System.Runtime.InteropServices.dll")" -o "$(cygpath -m "$work/FontFileZero.dll")"
  cmp "$original/NativePointerWrapper.cpp" "$work/NativePointerWrapper.cpp"
  cmp "$original/NativePointerWrapper.h" "$work/NativePointerWrapper.h"
  cmp "$original/OpenTypeTableTag.h" "$work/OpenTypeTableTag.h"
  /clang64/bin/clang++.exe -std=c++17 -shared -O2 "$here/wpf-native/FontFileProbe.cpp" -ldwrite -o "$work/font-file-probe.dll"
  cp "$here/wpf-tests/FontFileZero.cs" "$work/Program.cs"
  cat > "$work/FontFileZeroCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="FontFileZero"><HintPath>FontFileZero.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/FontFileZeroCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$work/font-file-probe.dll" "$work/bin/Release/net8.0/"
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/FontFileZeroCheck.dll")"
}

step_check_shaping_bits() {
  local work=$build/shaping-bits-check
  local original=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper/TextAnalyzer.cpp
  mkdir -p "$work"
  python3 - "$original" "$work/ShapingLoopOriginal.inc" <<'PY'
import pathlib, sys
source=pathlib.Path(sys.argv[1]).read_bytes()
assignment=source.index(b'pfCanGlyphAlone[i] = (((DWRITE_SHAPING_TEXT_PROPERTIES*)textProps)[i].isShapedAlone')
start=source.rfind(b'                        for (UINT32 i = 0; i < textLength; ++i)',0,assignment)
end=source.index(b'                        }',assignment)+len(b'                        }')
assert start>=0
pathlib.Path(sys.argv[2]).write_bytes(source[start:end])
assert pathlib.Path(sys.argv[2]).read_bytes()==source[start:end]
PY
  cp "$here/wpf-tests/ShapingBits.cpp" "$work/"
  /clang64/bin/clang++.exe -std=c++17 -O2 -shared "$here/wpf-native/ShapingBitsProbe.cpp" -ldwrite -o "$work/wpf-shaping-bits.dll"
  bridge_compile "$(cygpath -m "$work/ShapingBits.cpp")" --std c++14 --type System.GC \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --native-library wpf-shaping-bits.dll -o "$(cygpath -m "$work/ShapingBits.dll")"
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/ShapingBits.dll")"
}

step_check_range_nodes() {
  initialize
  local work=$build/range-nodes-check
  local original=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  python3 - "$original" "$work" <<'PY'
import pathlib, sys
source,work=map(pathlib.Path,sys.argv[1:])
header=(source/'TextItemizer.h').read_bytes()
start=header.index(b'    template<class T>')
end=header.index(b'    [ClassInterface',start)
node=header[start:end]
(work/'RangeNodeOriginal.inc').write_bytes(node)
body=(source/'TextItemizer.cpp').read_bytes()
start=body.index(b'    TextItemizer::TextItemizer(')
end=body.index(b'    __declspec(noinline) IList<Span^>^ TextItemizer::Itemize(',start)
first=body[start:end]
start=body.index(b'    void TextItemizer::SetIsDigit(')
end=body.index(b'}}}}//MS::Internal::Text::TextInterface',start)
last=body[start:end]
(work/'RangeMethodsOriginal.inc').write_bytes(first+last)
assert (work/'RangeNodeOriginal.inc').read_bytes()==node
assert (work/'RangeMethodsOriginal.inc').read_bytes()==first+last
PY
  cp "$here/wpf-tests/RangeNodes.cpp" "$work/"
  bridge_compile "$(cygpath -m "$work/RangeNodes.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --type System.GC --type System.UInt32 --type System.Math \
    --type System.InvalidOperationException --type 'System.Collections.Generic.List`1' \
    --reference "$(cygpath -m "$core_ref/System.Collections.dll")" -o "$(cygpath -m "$work/RangeNodes.dll")"
  cp "$here/wpf-tests/RangeNodes.cs" "$work/Program.cs"
  cat > "$work/RangeNodesCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="RangeNodes"><HintPath>RangeNodes.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/RangeNodesCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  timeout 45 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/RangeNodesCheck.dll")"
}

step_check_original_font_file() {
  initialize
  local work=$build/original-font-file-check
  local original=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  local files=(FontFile.cpp FontFile.h FontFileType.h FontFaceType.h NativePointerWrapper.cpp NativePointerWrapper.h)
  for name in "${files[@]}"; do cp "$original/$name" "$work/"; done
  # FontFile.cpp includes this converter but uses no declarations from it.
  printf '// Unused converter include in this focused FontFile dependency context.\n' > "$work/DWriteTypeConverter.h"
  python3 - "$original/Common.h" "$work/Common.h" <<'PY'
import pathlib, sys
source=pathlib.Path(sys.argv[1]).read_bytes()
start=source.index(b'    __declspec(noinline) static _GUID ToGUID(')
end=source.index(b'\nprivate:',start)
method=source[start:end]
prefix=b'''#pragma once
#include <windows.h>
#include <dwrite.h>
#include <vcclr.h>
using namespace System;
namespace MS { namespace Internal { namespace Text { namespace TextInterface { namespace Native {
private ref class Util abstract sealed {
public:
    static void ConvertHresultToException(HRESULT hr) { if(hr<0) System::Runtime::InteropServices::Marshal::ThrowExceptionForHR(hr); }
'''
suffix=b'''};
}}}}}
#define ConvertHresultToException(hr,msg) Native::Util::ConvertHresultToException(hr)
'''
pathlib.Path(sys.argv[2]).write_bytes(prefix+method+suffix)
assert method in pathlib.Path(sys.argv[2]).read_bytes()
PY
  cp "$here/wpf-tests/OriginalFontFile.cpp" "$work/"
  bridge_compile "$(cygpath -m "$work/OriginalFontFile.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --native-library font-file-probe.dll \
    --public-key "$(cygpath -m "$build/keys/MSShared.snk")" --type MS.Internal.Invariant \
    --type System.Runtime.InteropServices.CriticalHandle --type System.Runtime.InteropServices.Marshal --type System.Guid \
    --type System.Runtime.ConstrainedExecution.CriticalFinalizerObject \
    --type System.Runtime.ConstrainedExecution.Cer --type System.Runtime.ConstrainedExecution.Consistency \
    --type System.Runtime.ConstrainedExecution.ReliabilityContractAttribute --type System.GC \
    --type System.InvalidOperationException --type System.Exception \
    --reference "$(cygpath -m "$core_ref/System.Runtime.InteropServices.dll")" \
    --reference "$(cygpath -m "$build/lib/WindowsBase.dll")" \
    -D "WINDOWS_BASE_DLL=\"$(cygpath -m "$build/lib/WindowsBase.dll")\"" -o "$(cygpath -m "$work/DirectWriteForwarder.dll")"
  for name in "${files[@]}"; do cmp "$original/$name" "$work/$name"; done
  /clang64/bin/clang++.exe -std=c++17 -shared -O2 "$here/wpf-native/FontFileProbe.cpp" -ldwrite -o "$work/font-file-probe.dll"
  cp "$here/wpf-tests/OriginalFontFile.cs" "$work/Program.cs"
  cat > "$work/OriginalFontFileCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="DirectWriteForwarder"><HintPath>DirectWriteForwarder.dll</HintPath></Reference><Reference Include="WindowsBase"><HintPath>../lib/WindowsBase.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/OriginalFontFileCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$work/font-file-probe.dll" "$work/bin/Release/net8.0/"
  timeout 45 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/OriginalFontFileCheck.dll")"
}

step_check_native_com() {
  local work=$build/native-com-check
  mkdir -p "$work"
  /clang64/bin/clang++.exe -std=c++17 -O2 -shared "$here/wpf-native/DirectWriteBridge.cpp" -ldwrite -o "$work/wpf-directwrite-native.dll"
  bridge_compile "$(cygpath -m "$here/wpf-tests/NativeComDirectWrite.cpp")" --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --type System.GC \
    --native-library wpf-directwrite-native.dll -o "$(cygpath -m "$work/NativeComDirectWrite.dll")"
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/NativeComDirectWrite.dll")"
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

step_check_subsetter() {
  initialize
  local work=$build/subsetter-check
  local original=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/TrueTypeSubsetter
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  [ -s "$build/lib/libwpf-subsetter.a" ] || step_native_subsetter
  mkdir -p "$work"
  cp "$original/truetype.cpp" "$original/truetype.h" "$original/fsassert.h" "$work/"
  printf '#include <windows.h>\n#include <limits.h>\n#include <vcclr.h>\n' > "$work/precomp.hxx"
  bridge_compile "$(cygpath -m "$work/truetype.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --type System.Uri --type System.IntPtr \
    --type System.Runtime.InteropServices.Marshal --type System.IO.FileFormatException \
    --reference "$(cygpath -m "$core_ref/System.Runtime.InteropServices.dll")" \
    --reference "$(cygpath -m "$oob/System.IO.Packaging.dll")" \
    --native-import "$(cygpath -m "$build/lib/libwpf-subsetter.a")" \
    -D WPF_SUBSETTER_SPLIT=1 -I "$(cygpath -m "$here/wpf-native")" -I "$(cygpath -m "$original")" \
    -o "$(cygpath -m "$work/Subsetter.dll")"
  cp "$here/wpf-tests/Subsetter.cs" "$work/Program.cs"
  cat > "$work/SubsetterCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit><AllowUnsafeBlocks>true</AllowUnsafeBlocks></PropertyGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/SubsetterCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$work/Subsetter.dll" "$build/lib/wpf-subsetter.dll" "$oob/System.IO.Packaging.dll" "$work/bin/Release/net8.0/"
  timeout 60 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/SubsetterCheck.dll")"
}

step_check_registry_time() {
  initialize
  local work=$build/registry-time-check
  local original=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/TrueTypeSubsetter
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  # Compile the entire unchanged implementation and its original declaration.
  # This focused precompiled-header context supplies only their dependencies.
  cp "$original/util2.cpp" "$original/util2.h" "$work/"
  printf '#include <windows.h>\n#include <vcclr.h>\n' > "$work/precomp.hxx"
  bridge_compile "$(cygpath -m "$work/util2.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --type System.DateTime \
    --type System.Runtime.InteropServices.OutAttribute \
    --reference "$(cygpath -m "$core_ref/System.Runtime.InteropServices.dll")" \
    --native-import "$(cygpath -m /clang64/lib/libadvapi32.a)" -D UNICODE=1 -D _UNICODE=1 \
    -o "$(cygpath -m "$work/RegistryTime.dll")"
  cmp "$original/util2.cpp" "$work/util2.cpp"
  cmp "$original/util2.h" "$work/util2.h"
  cp "$here/wpf-tests/RegistryTime.cs" "$work/Program.cs"
  cat > "$work/RegistryTimeCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/RegistryTimeCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$work/RegistryTime.dll" "$work/bin/Release/net8.0/"
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/RegistryTimeCheck.dll")"
}

step_check_glyph_pointer() {
  initialize
  local work=$build/glyph-pointer-check
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  local header=$source/src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper/GlyphMetrics.h
  mkdir -p "$work"
  cat > "$work/GlyphPointer.cpp" <<EOF
using namespace System;
typedef int INT32;
typedef unsigned int UINT32;
#include "$(cygpath -m "$header")"
using namespace MS::Internal::Text::TextInterface;
extern "C" int NativeGlyphs(GlyphMetrics*, unsigned int);
public ref class WpfGlyphPointer abstract sealed {
public:
    static Object^ Allocate(int count) { return gcnew array<GlyphMetrics>(count); }
    static int Fill(IntPtr address, unsigned int count) {
        GlyphMetrics* pointer = reinterpret_cast<GlyphMetrics*>(address.ToPointer());
        GC::Collect();
        return NativeGlyphs(pointer, count);
    }
};
EOF
  /clang64/bin/clang++.exe -std=c++17 -O2 -shared "$here/wpf-tests/GlyphPointerNative.cpp" -o "$work/wpf-glyph-pointer.dll"
  bridge_compile "$(cygpath -m "$work/GlyphPointer.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --type System.GC \
    --type System.Runtime.InteropServices.StructLayoutAttribute --type System.Runtime.InteropServices.LayoutKind \
    --type System.Runtime.InteropServices.FieldOffsetAttribute --native-library wpf-glyph-pointer.dll \
    --reference "$(cygpath -m "$core_ref/System.Runtime.InteropServices.dll")" -o "$(cygpath -m "$work/GlyphPointer.dll")"
  cp "$here/wpf-tests/GlyphPointer.cs" "$work/Program.cs"
  cat > "$work/GlyphPointerCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="GlyphPointer"><HintPath>GlyphPointer.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/GlyphPointerCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$work/wpf-glyph-pointer.dll" "$work/bin/Release/net8.0/"
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/GlyphPointerCheck.dll")"
}

step_check_enumerator() {
  initialize
  local work=$build/enumerator-check
  local core_refs=("$sdk"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0)
  local core_ref=${core_refs[-1]}
  mkdir -p "$work"
  /clang64/bin/python.exe "$here/tools/wpf-enumerator-check.py" "$(cygpath -m "$source")" "$(cygpath -m "$work/Enumerator.cpp")"
  bridge_compile "$(cygpath -m "$work/Enumerator.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" \
    --type System.Threading.Monitor --type System.InvalidOperationException --type System.Globalization.CultureInfo \
    --type 'System.Collections.Generic.IEnumerator`1' --type 'System.Collections.Generic.KeyValuePair`2' \
    --type System.Collections.IEnumerator --type System.IDisposable \
    --reference "$(cygpath -m "$core_ref/System.Threading.dll")" \
    --reference "$(cygpath -m "$core_ref/System.Globalization.dll")" -o "$(cygpath -m "$work/Enumerator.dll")"
  cp "$here/wpf-tests/Enumerator.cs" "$work/Program.cs"
  cat > "$work/EnumeratorCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="Enumerator"><HintPath>Enumerator.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/EnumeratorCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/EnumeratorCheck.dll")"
}

step_check_native_guid() {
  initialize
  local work=$build/native-guid-check
  mkdir -p "$work"
  /usr/bin/python3 "$here/tools/wpf-native-guid-check.py" "$source" "$work/NativeGuid.cpp"
  /clang64/bin/clang++.exe -std=c++14 -O2 -shared "$here/cppcli/tests/native-storage-native.cpp" -o "$work/wpf-guid-native.dll"
  bridge_compile "$(cygpath -m "$work/NativeGuid.cpp")" --target library --std c++14 \
    --ilasm "$(cygpath -m /clang64/bin/ilasm-dotnet.exe)" --type System.Guid --type System.GC \
    --native-library wpf-guid-native.dll -o "$(cygpath -m "$work/NativeGuid.dll")"
  cp "$here/wpf-tests/NativeGuid.cs" "$work/Program.cs"
  cat > "$work/NativeGuidCheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Reference Include="NativeGuid"><HintPath>NativeGuid.dll</HintPath></Reference></ItemGroup></Project>
EOF
  "$sdk/dotnet.exe" build "$(cygpath -m "$work/NativeGuidCheck.csproj")" -c Release \
    --configfile "$(cygpath -m "$build/NuGet.config")" -p:UseSharedCompilation=false -v:q
  cp "$work/wpf-guid-native.dll" "$work/bin/Release/net8.0/"
  timeout 30 "$sdk/dotnet.exe" "$(cygpath -m "$work/bin/Release/net8.0/NativeGuidCheck.dll")"
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
