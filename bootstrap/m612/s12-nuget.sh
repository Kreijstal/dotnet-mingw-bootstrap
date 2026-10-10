#!/bin/bash
# S12: NuGet.Client 6.8.2.3 with its own source-build entry points (build/build.proj RestoreXPLAT,
# BuildXPLAT, PackXPLAT with DotNetBuildFromSource=true: the NuGet.* libraries, NuGet.Build.Tasks(.Console,
# .Pack), Microsoft.Build.NuGetSdkResolver, NuGet.CommandLine.XPlat ... for net8.0/netstandard2.0),
# package version 6.8.2-rc.3 (BuildNumber 3), the version SDK 8.0.131 pins.  NuGet is not Arcade-based;
# its build/config.props imports DotNetPackageVersionPropsPath itself.
# usage: s12-nuget.sh [prep] [build]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep build}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
V=6.8.2.3
X=$WORK/nuget
TAR=$M612/src/nuget-$V.tar.gz
step_prep() { s12_prep_repo "$TAR" "$X" "$here/../../patches/m612/s12-nuget-$V"; }
# IsXPlat=true: what NuGet's (Linux) source build sees; on Windows it would add Microsoft.SourceLink.GitHub
# 1.0.0 (a prebuilt package; the SDK has SourceLink built in) and the VS-only bits.
# Version overrides: System.Security.Cryptography.Pkcs 6.0.4 -> 7.0.2 and .Xml 6.0.1 -> 7.0.1, what the
# Microsoft.Build.Tasks.Core 17.8.49 package NuGet now compiles against requires (else NU1605 downgrade);
# System.ComponentModel.Composition 4.5.0 -> 7.0.0 (NuGet.PackageManagement only; SBRP has 7.0.0, the VMR
# lifts it to the runtime's 8.0 build).
# PathMap: NuGet is not Arcade-based, nothing maps its source root to /_/ (Arcade does that from SourceRoot).
# DebugType=portable: Release asks for pdbonly, which Roslyn writes as a portable PDB on Linux (the
# source build) but as a Windows PDB through the native Microsoft.DiaSymReader.Native on Windows.
# TreatWarningsAsErrors=false: .NET 8 obsoletions (SYSLIB0051) in NuGet 6.8 code (warnings only).
# the projects of NuGet's source build (XPLATProject=true in src/NuGet.Core), as a solution filter;
# build/build.proj's XPLAT targets assume a Unix path (IsXPlat) and on Windows restore the test projects too
step_build() {
  cd "$X"
  python3 -S - "$X/s12.slnf" <<'EOPY'
import json, os, re, sys
root = os.path.dirname(sys.argv[1]); ps = []
for d in sorted(os.listdir(os.path.join(root, 'src', 'NuGet.Core'))):
    f = os.path.join(root, 'src', 'NuGet.Core', d, d + '.csproj')
    if os.path.exists(f) and re.search(r'<XPLATProject>\s*true', open(f, encoding='utf-8-sig').read()):
        ps.append('src\\NuGet.Core\\%s\\%s.csproj' % (d, d))
json.dump({'solution': {'path': 'NuGet.sln', 'projects': ps}}, open(sys.argv[1], 'w', newline='\n'), indent=2)
print(len(ps), 'projects')
EOPY
  s12_build nuget "$X" s12.slnf -p:BuildNumber=3 -p:RepositoryCommit=$(repo_commit "$TAR") -p:SourceRevisionId=$(repo_commit "$TAR") -p:RepositoryUrl=https://github.com/NuGet/NuGet.Client -p:IsXPlat=true \
    -p:SystemSecurityCryptographyPkcsVersion=7.0.2 -p:SystemSecurityCryptographyXmlVersion=7.0.1 \
    -p:SystemComponentModelCompositionPackageVersion=7.0.0 -p:DebugType=portable -p:PathMap="$(w "$X")=/_/" -p:TreatWarningsAsErrors=false -p:MSBuildTreatWarningsAsErrors=false
}
for s in $steps; do log "s12 nuget $V (stage $STAGE): $s"; "step_$s"; done
