#!/bin/bash
# S12: dotnet/templating v8.0.131 (Microsoft.TemplateEngine.* / TemplateSearch.Common 8.0.131, what the SDK's
# `dotnet new` runs) with `dotnet build/pack` of its src/ projects (not its Microsoft.TemplateEngine.Cli: the
# SDK builds its own).  Version overrides: Microsoft.Extensions.Logging(.Abstractions/.Console),
# DependencyInjection.Abstractions 8.0.x -> 6.0.0 and System.Formats.Asn1 8.0.1 -> 7.0.0, the
# source-build-reference-packages versions: the runtime's 8.0 packages are not built from their project
# files here and the netstandard2.0 targets need a netstandard2.0 asset (the net8.0 SDK runs them against
# its own 8.0 assemblies).
# TreatWarningsAsErrors=false: NU5104 (the stable 8.0.131 packages depend on NuGet 6.8.2-rc.3, the NuGet
# version SDK 8.0.131 itself pins).
# usage: s12-templating.sh [prep] [build]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep build}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
X=$WORK/templating
TAR=$M612/src/templating-8.0.131.tar.gz
step_prep() {
  s12_prep_repo "$TAR" "$X" "$here/../../patches/m612/s12-templating"
  python3 -S - "$X/s12.slnf" <<'EOPY'
import json, os, sys
root = os.path.dirname(sys.argv[1]); ps = []
for d in sorted(os.listdir(os.path.join(root, 'src'))):
    f = os.path.join(root, 'src', d, d + '.csproj')
    if os.path.exists(f) and d != 'Microsoft.TemplateEngine.Cli':
        ps.append('src\\%s\\%s.csproj' % (d, d))
json.dump({'solution': {'path': 'Microsoft.TemplateEngine.sln', 'projects': ps}}, open(sys.argv[1], 'w', newline='\n'), indent=2)
EOPY
}
step_build() {
  s12_build templating "$X" s12.slnf -p:OfficialBuildId=20260811.1 -p:RepositoryCommit=$(repo_commit "$TAR") \
    -p:MicrosoftExtensionsLoggingPackageVersion=6.0.0 -p:MicrosoftExtensionsLoggingAbstractionsPackageVersion=6.0.0 \
    -p:MicrosoftExtensionsLoggingConsolePackageVersion=6.0.0 -p:MicrosoftExtensionsDependencyInjectionAbstractionsPackageVersion=6.0.0 \
    -p:SystemFormatsAsn1Version=7.0.0 -p:TreatWarningsAsErrors=false
}
for s in $steps; do log "s12 templating (stage $STAGE): $s"; "step_$s"; done
