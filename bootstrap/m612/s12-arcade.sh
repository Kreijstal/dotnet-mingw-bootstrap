#!/bin/bash
# S12: the Arcade SDK (Microsoft.DotNet.Arcade.Sdk, dotnet/arcade 8.0.0-beta.26411.4, the version
# dotnet/sdk 8.0.131 pins) from source, with the SDK of the previous stage:
#   prep   extract dotnet/arcade into s<N>/work/arcade, the snk key *pairs* in the SDK's tools/snk
#          replaced by public-key-only files (the build only public-signs)
#   boot   a bootstrap Arcade SDK "8.0.0-boot" (s<N>/boot/feed): the SDK's MSBuild text (sdk/, tools/)
#          as it is in the tree + its task assembly (tools/net8.0/Microsoft.DotNet.Arcade.Sdk.dll)
#          compiled by a small SDK-style project of ours from the same sources, against the building
#          SDK's own MSBuild/NuGet assemblies.  Needed once: Arcade builds itself with Arcade.
#   build  the real thing: `dotnet build` of src/Microsoft.DotNet.Arcade.Sdk/Microsoft.DotNet.Arcade.Sdk.csproj
#          (+ its ProjectReferences) under the bootstrap SDK, as in source-build: DotNetBuildFromSource,
#          compiled against source-build-reference-packages (s12-sbrp.sh refs); the package
#          (GeneratePackageOnBuild) goes to the stage feed as Microsoft.DotNet.Arcade.Sdk 8.0.0-beta.26411.4
#          (OfficialBuildId 20260811.4 gives Arcade's own version number).  One version override:
#          Microsoft.Extensions.Logging.Console 8.0.1 -> 6.0.0 (SBRP), used by Microsoft.Arcade.Common
#          (netstandard2.0) only, which the SDK's tasks do not use; the VMR takes 8.0.1 from the
#          previously-source-built tarball, our runtime libraries have no netstandard2.0 build
#
# usage: s12-arcade.sh [step...]   (default: prep boot build)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep boot build}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
A=$WORK/arcade
BOOT=$SN/boot
SDKV=8.0.131

step_prep() {
  local t=$S12/src/arcade-$ARCADE_SHA.tar.gz
  fetch_tarball "https://github.com/dotnet/arcade/archive/$ARCADE_SHA.tar.gz" "$t"
  s12_extract "$t" "$A"
  # tools/snk: Open.snk, AspNetCore.snk, cecil.snk are full RSA key pairs (published, but private
  # key material is not needed: everything is public-signed); use the public keys of keys/
  local T=$A/src/Microsoft.DotNet.Arcade.Sdk/tools/snk
  cp "$(cygpath -u "$(keyfile Open)")" "$T/Open.snk"
  cp "$(cygpath -u "$(keyfile MicrosoftAspNetCore)")" "$T/AspNetCore.snk"
  cp "$(cygpath -u "$(keyfile Cecil)")" "$T/cecil.snk"
  apply_patches "$A" "$here/../../patches/m612/arcade"
}

step_boot() {
  local P=$BOOT/arcade-pkg O=$BOOT/arcade-tasks SDKDIR=$BUILD_SDK/sdk/$SDKV
  rm -rf "${BOOT:?}"; mkdir -p "$P" "$O" "$BOOT/feed"
  # the task assembly: same sources as Microsoft.DotNet.Arcade.Sdk.csproj (src/**, AssemblyResolution.cs;
  # InstallDotNetCore.cs is left out under DotNetBuildFromSource there too)
  cat > "$O/Microsoft.DotNet.Arcade.Sdk.csproj" <<EOF
<Project Sdk="Microsoft.NET.Sdk">
  <!-- m612 S12 bootstrap only (s12-arcade.sh boot): Arcade's task assembly without Arcade -->
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <AssemblyName>Microsoft.DotNet.Arcade.Sdk</AssemblyName>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <LangVersion>latest</LangVersion>
    <Deterministic>true</Deterministic>
    <NoWarn>\$(NoWarn);3021;SYSLIB0013;CS8632</NoWarn>
    <DefineConstants>\$(DefineConstants);DOTNET_BUILD_FROM_SOURCE</DefineConstants>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="$(m "$A")/src/Microsoft.DotNet.Arcade.Sdk/src/**/*.cs" Exclude="$(m "$A")/src/Microsoft.DotNet.Arcade.Sdk/src/InstallDotNetCore.cs" />
    <Compile Include="$(m "$A")/src/Common/Internal/AssemblyResolution.cs" />
    <Reference Include="$(m "$SDKDIR")/Microsoft.Build.dll" Private="false" />
    <Reference Include="$(m "$SDKDIR")/Microsoft.Build.Framework.dll" Private="false" />
    <Reference Include="$(m "$SDKDIR")/Microsoft.Build.Utilities.Core.dll" Private="false" />
    <Reference Include="$(m "$SDKDIR")/Microsoft.Build.Tasks.Core.dll" Private="false" />
    <Reference Include="$(m "$SDKDIR")/NuGet.Versioning.dll" Private="false" />
  </ItemGroup>
</Project>
EOF
  s12_nugetconfig "$O"
  dn build "$(w "$O/Microsoft.DotNet.Arcade.Sdk.csproj")" -c Release -nologo -v:m -nodeReuse:false -o "$(w "$O/bin")"
  # the package: sdk/ + tools/ as Microsoft.DotNet.Arcade.Sdk.csproj packs them
  cp -r "$A/src/Microsoft.DotNet.Arcade.Sdk/sdk" "$A/src/Microsoft.DotNet.Arcade.Sdk/tools" "$P/"
  mkdir -p "$P/tools/net8.0"; cp "$O/bin/Microsoft.DotNet.Arcade.Sdk.dll" "$P/tools/net8.0/"
  python3 -S - "$A/eng/Versions.props" "$P/tools/DefaultVersions.Generated.props" <<'EOPY'
import re, sys
s = open(sys.argv[1]).read()
def v(n):
    m = re.search(r'<%s>([^<]*)</%s>' % (n, n), s); return m.group(1) if m else ''
names = ['MicrosoftNetCompilersToolsetVersion', 'MicrosoftNetILLinkTasksVersion', 'MicrosoftDiaSymReaderPdb2PdbVersion',
         'MicrosoftDotNetXliffTasksVersion', 'MicrosoftDotNetMaestroTasksVersion', 'MicrosoftSymbolUploaderBuildTaskVersion',
         'MicrosoftTemplateEngineAuthoringTasksVersion']
o = ['<!-- Generated by m612 s12-arcade.sh boot (Microsoft.DotNet.Arcade.Sdk.csproj _GenerateSdkVersionFile) -->',
     '<Project>', '  <PropertyGroup>', '    <ArcadeSdkVersion>8.0.0-boot</ArcadeSdkVersion>']
o += ['    <%s>%s</%s>' % (n, v(n), n) for n in names]
o += ['    <MicrosoftDotNetXUnitAssertVersion>8.0.0-boot</MicrosoftDotNetXUnitAssertVersion>', '  </PropertyGroup>', '</Project>']
open(sys.argv[2], 'w', newline='\n').write('\n'.join(o) + '\n')
EOPY
  cat > "$P/microsoft.dotnet.arcade.sdk.nuspec" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2013/05/nuspec.xsd">
  <metadata>
    <id>Microsoft.DotNet.Arcade.Sdk</id>
    <version>8.0.0-boot</version>
    <authors>Microsoft</authors>
    <description>m612 S12 bootstrap Arcade SDK (dotnet/arcade sources; replaced by the real build)</description>
    <developmentDependency>true</developmentDependency>
    <packageTypes><packageType name="MSBuildSdk" /></packageTypes>
  </metadata>
</package>
EOF
  python3 -S "$M612_TOOLS_SRC/s12nupkg.py" "$P" "$P/microsoft.dotnet.arcade.sdk.nuspec" "$BOOT/feed"
}

step_build() {
  cd "$A"
  python3 -S "$M612_TOOLS_SRC/s12globaljson.py" global.json "$SDKV" Microsoft.DotNet.Arcade.Sdk=8.0.0-boot
  s12_nugetconfig "$A" "$BOOT/feed"
  versions_props
  rm -rf artifacts
  local t0=$SECONDS
  dn build src/Microsoft.DotNet.Arcade.Sdk/Microsoft.DotNet.Arcade.Sdk.csproj -c Release -nologo -v:m -nodeReuse:false \
    $(s12_props) -p:OfficialBuildId=20260811.4 -p:MicrosoftExtensionsLoggingConsoleVersion=6.0.0 \
    -bl:"$(w "$S12/logs/arcade-s$STAGE.binlog")"
  echo "TIME arcade build $((SECONDS - t0)) s"
  local p; p=$(ls artifacts/packages/Release/*/Microsoft.DotNet.Arcade.Sdk.*.nupkg)
  feed_add $p; ls -la $p
}

for s in $steps; do log "s12 arcade (stage $STAGE): $s"; "step_$s"; done
