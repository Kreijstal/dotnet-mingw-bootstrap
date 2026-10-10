#!/bin/bash
# S12: MSBuild 17.8.49 with `dotnet build` of its own project files (MSBuild.SourceBuild.slnf:
# Microsoft.Build{,.Framework,.Utilities.Core,.Tasks.Core}, Microsoft.NET.StringTools, MSBuild.dll and
# the localization package), net8.0 (+ the netstandard2.0 reference builds of Framework/Utilities/
# StringTools), package version 17.8.49 (DotNetFinalVersionKind=release; OfficialBuildId 20251216.8 is
# the build behind Microsoft.Build.Localization 17.8.49-servicing-25616-08 that SDK 8.0.131 pins).
# Package validation (API compatibility against the previous release, Microsoft.Build* 17.7.0 from
# nuget.org) is off: it needs the old packages and only checks.
# The S5 patch (patches/m612/msbuild-17.8.49, Mono code paths) is not applied: nothing here runs on Mono.
# usage: s12-msbuild.sh [prep] [build]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep build}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
V=17.8.49
X=$WORK/msbuild
TAR=$M612/src/msbuild-$V.tar.gz
step_prep() { s12_prep_repo "$TAR" "$X" "$here/../../patches/m612/s12-msbuild-$V"; }
step_build() {
  s12_build msbuild "$X" MSBuild.SourceBuild.slnf -p:OfficialBuildId=20251216.8 -p:RepositoryCommit=$(repo_commit "$TAR") \
    -p:UsingToolMicrosoftNetCompilers=false -p:EnablePackageValidation=false
}
for s in $steps; do log "s12 msbuild $V (stage $STAGE): $s"; "step_$s"; done
