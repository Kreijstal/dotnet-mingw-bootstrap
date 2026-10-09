#!/bin/bash
# S11: third-party and satellite-repo libraries the net8.0 SDK tools need, from
# source, compiled for .NET 8 (sdkproj.sh) into $M612/net8-pkgs:
#   newtonsoft   Newtonsoft.Json 13.0.3 (its net6.0 flavour)          NuGet, templating, dotnet CLI
#   hostmodel    Microsoft.NET.HostModel (runtime 8.0.0)              SDK tasks (apphost), CLI
#   releases     Microsoft.Deployment.DotNet.Releases (deployment-tools) SDK tasks, CLI
#   commandline  System.CommandLine 2.0.0-beta4.23307.1                dotnet CLI, TemplateEngine.Cli
#   appinsights  Microsoft.ApplicationInsights 2.21.0                  dotnet CLI telemetry (opt-out)
#   templating   Microsoft.TemplateEngine.* 8.0.131 (dotnet/templating) dotnet new; needs nuget-net8.sh
#
# usage: clideps-net8.sh [step...]     (default: newtonsoft hostmodel releases commandline appinsights)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-newtonsoft hostmodel releases commandline appinsights}
BCSC=${BCSC:-$M612/roslyn-4.8.0/csc.exe}   # the Roslyn of SDK 8.0.1xx; the from-source generators are built against it
B=$M612/build/clideps-net8; mkdir -p "$B/obj"
. "$here/common.sh"
. "$here/sdkproj.sh"
S=$M612/src
OUTP=$M612/net8-pkgs
NUGET8=${NUGET8:-$M612/nuget-6.8.2.3-net8}
SP_OUT=$OUTP; SP_REFS=(); SP_GROUP=clideps; mkdir -p "$OUTP"

src_tree() { # src_tree <dir> <tarball> : extract once, provenance scan
  [ -d "$S/$1" ] && [ -n "$(ls -A "$S/$1")" ] || { mkdir -p "$S/$1"; tar -C "$S/$1" --strip-components=1 -xzf "$S/$2"; }
  [ -f "$S/$1/.deleted-binaries.txt" ] || delete_binaries "$S/$1"
}

step_newtonsoft() {
  local NJ=$S/newtonsoft.json-13.0.3
  [ -d "$NJ/Src" ] || die "Newtonsoft.Json sources missing (netfx-pkgs.sh Newtonsoft.Json fetches them)"
  SP_TF=net6.0; SP_EVAL=(-p LibraryFrameworks=net6.0); SP_KEY=Newtonsoft; SP_VER=13.0.0.0; SP_FILEVER=13.0.3.27908; SP_INFOVER=13.0.3; SP_DEFS=(-d:SIGNED)
  sdkproj "$NJ/Src/Newtonsoft.Json/Newtonsoft.Json.csproj"
}
step_hostmodel() {
  SP_TF=netstandard2.0; SP_EVAL=(-p CoreClrProjectRoot=$S/runtime-8.0.0/src/coreclr/); SP_KEY=MicrosoftAspNetCore; SP_VER=8.0.0.0; SP_FILEVER=8.0.0.0; SP_INFOVER=8.0.0; SP_DEFS=()
  sdkproj "$S/runtime-8.0.0/src/installer/managed/Microsoft.NET.HostModel/Microsoft.NET.HostModel.csproj"
}
step_releases() {
  local DT; DT=$(ls -d "$S"/deployment-tools-*/ | head -1)
  SP_TF=netstandard2.0; SP_EVAL=(); SP_KEY=Microsoft; SP_VER=2.0.0.0; SP_FILEVER=2.0.0.0; SP_INFOVER=2.0.0-rtm.1.25064.1; SP_DEFS=()
  sdkproj "$DT/src/Microsoft.Deployment.DotNet.Releases/src/Microsoft.Deployment.DotNet.Releases.csproj"
}
step_commandline() {
  src_tree command-line-api-02fe27cd command-line-api-02fe27cd.tar.gz
  SP_TF=net8.0; SP_EVAL=(-p TargetFrameworkForNETSDK=net8.0 -p NetCurrent=net8.0); SP_KEY=MSShared; SP_VER=2.0.0.0; SP_FILEVER=2.0.23.30701; SP_INFOVER=2.0.0-beta4.23307.1; SP_DEFS=()
  sdkproj "$S/command-line-api-02fe27cd/src/System.CommandLine/System.CommandLine.csproj"
}
step_appinsights() {
  local AI=$S/applicationinsights-2.21.0
  src_tree applicationinsights-2.21.0 applicationinsights-2.21.0.tar.gz
  SP_TF=netstandard2.0; SP_EVAL=(-p PropsRoot=$AI/.props -p OS=Windows_NT); SP_KEY=MSShared; SP_VER=2.21.0.429; SP_FILEVER=2.21.0.429; SP_INFOVER=2.21.0; SP_DEFS=()
  sdkproj "$AI/BASE/src/Microsoft.ApplicationInsights/Microsoft.ApplicationInsights.csproj"
}
step_templating() {
  local T=$S/templating-8.0.131 p
  src_tree templating-8.0.131 templating-8.0.131.tar.gz
  [ -f "$NUGET8/NuGet.Protocol.dll" ] || die "run nuget-net8.sh first"
  for p in Microsoft.TemplateEngine.Abstractions Microsoft.TemplateEngine.Utils Microsoft.TemplateEngine.Core.Contracts Microsoft.TemplateEngine.Core \
      Microsoft.TemplateEngine.Edge Microsoft.TemplateEngine.Orchestrator.RunnableProjects Microsoft.TemplateEngine.IDE Microsoft.TemplateSearch.Common; do
    SP_TF=netstandard2.0; SP_KEY=MicrosoftAspNetCore; SP_VER=8.0.131.0; SP_FILEVER=8.0.131.0; SP_INFOVER=8.0.131; SP_DEFS=(); SP_REFS=("$NUGET8")
    SP_EVAL=(-p RepoRoot=$T/ -p SrcDir=$T/src/ -p StrongNameKeyId=MicrosoftAspNetCore --pre "$T/Directory.Build.props")
    sdkproj "$T/src/$p/$p.csproj"
  done
}

for s in $steps; do log "clideps net8.0: $s"; "step_$s"; done
