#!/bin/bash
# Source-only Windows Forms bootstrap. Requires the source-built SDK and runtime OOB libraries.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
version=${WINFORMS_VERSION:-8.0.0}
source=${WINFORMS_SOURCE:-$M612/src/winforms-$version}
build=${WINFORMS_BUILD:-$M612/build/winforms-$version}
sdk=${WINFORMS_SDK:-/clang64/lib/dotnet}
oob=${WINFORMS_OOB:-$M612/oob-8.0.0/lib}
mkdir -p "$build/lib" "$build/keys"
steps=${*:-fetch drawing accessibility}
step_fetch() {
  local hash url archive=$M612/src/winforms-$version.tar.gz
  case $version in
    8.0.0) hash=bf2e48f2a35ab30f135ad91cfb3062f402d4fb84e5a36dead53dc89b81369ed2 ;;
    7.0.20) hash=7a95a414898b0e4d7e5aab657b3b60b344e67dfeba8a876eec14b010e1a7f49e ;;
    *) die "Unsupported/unpinned source release: $version" ;;
  esac
  url=https://codeload.github.com/dotnet/winforms/tar.gz/refs/tags/v$version
  [ -f "$archive" ] || curl -fL "$url" -o "$archive"
  echo "$hash  $archive" | sha256sum -c -
  if [ ! -f "$source/.source-bootstrap" ]; then
    mkdir -p "$source"
    tar -xf "$archive" --strip-components=1 -C "$source"
    scan_binaries "$source" > "$build/deleted-binaries.txt" || true
    tr '\n' '\0' < "$build/deleted-binaries.txt" | xargs -0 -r rm -f
    [ -z "$(scan_binaries "$source")" ] || die "binary inputs remain"
    printf '%s\n' "$hash" > "$source/.source-bootstrap"
  fi
}
initialize() {
  cat > "$build/NuGet.config" <<'EOF'
<configuration><packageSources><clear /></packageSources></configuration>
EOF
  /usr/bin/python3 - "$here/keys" "$build/keys" <<'PYKEYS'
from pathlib import Path
import sys
source,dest=map(Path,sys.argv[1:])
(dest/'ECMA.snk').write_bytes(bytes.fromhex('00000000000000000400000000000000'))
(dest/'Open.snk').write_bytes(bytes.fromhex(next(source.glob('Open-*.pubkey.hex')).read_text()))
PYKEYS
  if [ ! -f "$build/tools/bin/Release/net8.0/ResxGen.dll" ]; then
    mkdir -p "$build/tools"
    cp "$here/tools/ResxGen.cs" "$build/tools/"
    cat > "$build/tools/ResxGen.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup></Project>
EOF
    "$sdk/dotnet.exe" build "$(cygpath -m "$build/tools/ResxGen.csproj")" -c Release --configfile "$(cygpath -m "$build/NuGet.config")" -p:NuGetAudit=false -p:UseSharedCompilation=false -v:q
  fi
}
build_assembly() {
  local name=$1
  initialize
  /usr/bin/python3 "$here/tools/winformsplan.py" "$source" "$build" "$sdk" "$oob" "$build/keys" "$name" ${WINFORMS_BINDINGS:+--bindings "$WINFORMS_BINDINGS"}
  /usr/bin/python3 - "$build/$name/resources.json" "$sdk/dotnet.exe" "$build/tools/bin/Release/net8.0/ResxGen.dll" <<'PYGEN'
import json,subprocess,sys
for args in json.load(open(sys.argv[1])):
    subprocess.run(sys.argv[2:]+args,check=True)
PYGEN
  "$sdk/dotnet.exe" build "$(cygpath -m "$build/$name/$name.csproj")" -c Release --configfile "$(cygpath -m "$build/NuGet.config")" -v:q -p:UseSharedCompilation=false -p:NuGetAudit=false > "$build/$name/build.log" 2>&1 || {
    grep -E 'error ' "$build/$name/build.log" | head -40 || true
    die "source build failed: $name (see $build/$name/build.log)"
  }
  cp "$build/$name/bin/Release/net8.0/$name.dll" "$build/lib/"
  log "Built $name from source"
}
step_drawing() { build_assembly System.Drawing.Common; }
step_analyzers() { build_assembly System.Windows.Forms.Analyzers.CSharp; }
step_generator() { build_assembly System.Windows.Forms.PrivateSourceGenerators; }
step_primitives() { build_assembly System.Windows.Forms.Primitives; }
step_forms() { build_assembly System.Windows.Forms; }
step_accessibility() {
  initialize
  mkdir -p "$build/Accessibility"
  /clang64/bin/clang.exe -E -P -x c \
    "$source/src/Accessibility/src/Accessibility.il" -o "$build/Accessibility/Accessibility.il"
  # The upstream IL project substitutes these macros without C string quotes.
  # Apply the same substitution after preprocessing and replace its legacy version.
  sed -i -e 's/"System.Runtime"/System.Runtime/g' -e 's/"System.Runtime.InteropServices"/System.Runtime.InteropServices/g' \
    -e 's/\.ver 4:0:0:0/.ver 8:0:0:0/g' "$build/Accessibility/Accessibility.il"
  /clang64/bin/ilasm-dotnet.exe -dll "$(cygpath -m "$build/Accessibility/Accessibility.il")" \
    "-output=$(cygpath -m "$build/lib/Accessibility.dll")" "-key=$(cygpath -m "$build/keys/ECMA.snk")" > "$build/Accessibility/build.log" 2>&1 || { cat "$build/Accessibility/build.log"; die "Accessibility source build failed"; }
}
step_smoke() {
  mkdir -p "$build/smoke"
  cp "$here/winforms-tests/Program.cs" "$build/smoke/Program.cs"
  /usr/bin/python3 - "$build" "$oob" <<'PYPROJECT'
import sys,xml.etree.ElementTree as E,subprocess
from pathlib import Path
b,o=map(Path,sys.argv[1:])
r=E.Element('Project',Sdk='Microsoft.NET.Sdk');p=E.SubElement(r,'PropertyGroup')
for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','UseAppHost':'false','EnableNETAnalyzers':'false','EnableUnsafeBinaryFormatterSerialization':'true'}.items(): E.SubElement(p,k).text=v
ig=E.SubElement(r,'ItemGroup')
libs={f.stem:f for f in o.glob('*.dll')};libs.update({f.stem:f for f in (b/'lib').glob('*.dll')})
for n in ('Accessibility','System.Drawing.Common','System.Windows.Forms','System.Windows.Forms.Primitives','Microsoft.Win32.SystemEvents','System.Configuration.ConfigurationManager','System.Diagnostics.EventLog','System.Security.Cryptography.ProtectedData','System.Resources.Extensions','System.Windows.Extensions'):
 f=libs[n];item=E.SubElement(ig,'Reference',Include=n);E.SubElement(item,'HintPath').text=subprocess.check_output(['cygpath','-m',str(f)],text=True).strip()
E.ElementTree(r).write(b/'smoke/smoke.csproj',encoding='utf-8')
PYPROJECT
  "$sdk/dotnet.exe" build "$(cygpath -m "$build/smoke/smoke.csproj")" --configfile "$(cygpath -m "$build/NuGet.config")" -v:q -p:UseSharedCompilation=false -p:NuGetAudit=false
  timeout 60 "$sdk/dotnet.exe" "$(cygpath -m "$build/smoke/bin/Debug/net8.0/smoke.dll")"
}
step_layout() {
  local wpfsrc=${WINFORMS_WPF_SOURCE:-$M612/src/wpf-8.0.0} archive=$M612/src/wpf-8.0.0.tar.gz
  [ -f "$archive" ] || curl -fL https://codeload.github.com/dotnet/wpf/tar.gz/refs/tags/v8.0.0 -o "$archive"
  echo "b4acc515805fd551fe9982f496e4062f0e9ddc4bed219a236454857b56126748  $archive" | sha256sum -c -
  if [ ! -d "$wpfsrc/packaging" ]; then mkdir -p "$wpfsrc"; tar -xf "$archive" --strip-components=1 -C "$wpfsrc"; fi
  initialize
  mkdir -p "$build/metadata"
  cp "$here/cppcli/metadata/Program.cs" "$build/metadata/Program.cs"
  cp "$here/cppcli/metadata/metadata.csproj" "$build/metadata/metadata.csproj"
  "$sdk/dotnet.exe" build "$(cygpath -m "$build/metadata/metadata.csproj")" -c Release --configfile "$(cygpath -m "$build/NuGet.config")" -v:q -p:UseSharedCompilation=false -p:NuGetAudit=false
  local args=() file
  for file in "$oob"/*.dll "$build/lib"/*.dll; do args+=("$(cygpath -m "$file")"); done
  "$sdk/dotnet.exe" "$(cygpath -m "$build/metadata/bin/Release/net8.0/metadata.dll")" \
    "$(cygpath -m "$build/identities.json")" "$(cygpath -m "$build/identities.hpp")" '' "${args[@]}"
  /usr/bin/python3 "$here/tools/winforms-layout.py" "$build" "$oob" "$source" "$wpfsrc" "${WINFORMS_LAYOUT:-$build/layout}" --metadata "$build/identities.json"
}
for step in $steps; do "step_$step"; done
