# S12 (the SDK rebuilds itself from the upstream project files): shared settings.
# Source after env-m612.sh.
#
# Layout under $S12 (= $M612/s12):
#   src/                 tarballs + pristine extracted trees (arcade, xliff-tasks, sbrp, ...)
#   feed-sbrp/           source-build-reference-packages: reference/targeting/text-only packages built
#                        from the SBRP text (C#, IL, nuspecs); compile-time only, never lifted
#   feed-seed/           seed packages: assemblies of the hand-made S8-S11 drivers packed under their
#                        upstream id/version (s12-feed.sh seed); used only where no real build exists
#   s<N>/feed/           stage N's feed: hard links of feed-seed + the packages stage N built with
#                        `dotnet build/pack` (they replace a seed of the same id/version)
#   s<N>/packages/       NUGET_PACKAGES of stage N (fresh per stage)
#   s<N>/work/<repo>/    the build trees (extracted + patched) of stage N
#   s<N>/versions.props  DotNetPackageVersionPropsPath: the versions in s<N>/feed (source-build's
#                        version "lifting": imported by Arcade right after the repo's eng/Versions.props)
#   home/                DOTNET_CLI_HOME
#
# STAGE=2: built by BUILD_SDK (the stage-1 SDK, CoreCLR + R2R); STAGE=3: built by the stage-2 SDK.
S12=${S12:-$M612/s12}
STAGE=${STAGE:-2}
SN=$S12/s$STAGE
if [ "$STAGE" = 2 ]; then BUILD_SDK=${BUILD_SDK:-$M612/dotnet-sdk-8.0.131-net8-coreclr-r2r}
else BUILD_SDK=${BUILD_SDK:-$M612/dotnet-sdk-8.0.131-stage$((STAGE - 1))}; fi
FEED_SBRP=$S12/feed-sbrp
FEED_SEED=$S12/feed-seed
FEED=$SN/feed
PKGS=$SN/packages
WORK=$SN/work
VERPROPS=$SN/versions.props
S12_FILES=$(cd "$(dirname "${BASH_SOURCE[0]}")/s12" && pwd)
ILASM=${ILASM:-$M612/build/coreclr-8.0.0/cmake/ilasm/ilasm.exe}
mkdir -p "$S12/src" "$FEED_SBRP" "$FEED_SEED" "$FEED" "$PKGS" "$WORK" "$S12/home" "$S12/logs"

# s12 versions of the repos (tarball commits)
ARCADE_SHA=7945b1b660dff1927e4d71aca0a11e5d067ba4a5      # 8.0.0-beta.26411.4, the Arcade sdk 8.0.131 pins
ARCADE_VER=8.0.0-beta.26411.4
SBRP_SHA=5aeee2738338530a92d7f6caa2c7d5f569fed3c4        # source-build-reference-packages release/8.0
XLIFF_SHA=73f0850939d96131c28cf6ea6ee5aacb4da0083a       # xliff-tasks 1.0.0-beta.23475.1

w() { cygpath -w "$1"; }
m() { cygpath -m "$1"; }

# the environment every S12 dotnet invocation runs in: no telemetry, a private CLI home and package
# cache, and no network: any HTTP(S) request goes to a closed local port (restore can only use the
# local feeds; nuget.org from the user's NuGet.Config is <clear/>ed by every repo NuGet.config we write)
s12_env() {
  export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1 DOTNET_MULTILEVEL_LOOKUP=0
  export DOTNET_CLI_HOME; DOTNET_CLI_HOME=$(w "$S12/home")
  export NUGET_PACKAGES; NUGET_PACKAGES=$(w "$PKGS")
  export NUGET_HTTP_CACHE_PATH; NUGET_HTTP_CACHE_PATH=$(w "$S12/home/http-cache")
  export HTTP_PROXY=http://127.0.0.1:9 HTTPS_PROXY=http://127.0.0.1:9 NO_PROXY=
  export DOTNET_ROOT; DOTNET_ROOT=$(w "$BUILD_SDK")
  unset MSBuildSDKsPath MSBUILD_EXE_PATH DOTNET_HOST_PATH
}
dn() { ( s12_env; "$BUILD_SDK/dotnet.exe" "$@" ); }

# s12_nugetconfig <dir> [extra-feed...]: the only package sources are the local feeds
s12_nugetconfig() {
  local f d=$1 extra="" i=0; shift
  for f in "$@"; do i=$((i + 1)); extra+="    <add key=\"s12-extra$i\" value=\"$(w "$f")\" />"$'\n'; done
  set -- "$d"
  for f in "$1"/NuGet.config "$1"/NuGet.Config "$1"/nuget.config; do [ -f "$f" ] && rm -f "$f"; done
  cat > "$1/NuGet.config" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<!-- m612 S12: written by s12-common.sh; local source-built feeds only (no network) -->
<configuration>
  <packageSources>
    <clear />
    <add key="s12-stage$STAGE" value="$(w "$FEED")" />
    <add key="s12-sbrp" value="$(w "$FEED_SBRP")" />
${extra}  </packageSources>
  <disabledPackageSources>
    <clear />
  </disabledPackageSources>
  <packageSourceMapping>
    <clear />
  </packageSourceMapping>
</configuration>
EOF
}

# feed_add <nupkg...>: put packages built by stage N into its feed (replacing a seed of the same name)
feed_add() { local p; for p in "$@"; do rm -f "$FEED/$(basename "$p" | tr 'A-Z' 'a-z')" "$FEED/$(basename "$p")"; cp "$p" "$FEED/"; done; }
# feed_link_seed: hard-link the seed packages into the stage feed (where the stage has not built them)
feed_link_seed() {
  local p b; for p in "$FEED_SEED"/*.nupkg; do [ -e "$p" ] || continue; b=$(basename "$p")
    [ -e "$FEED/$b" ] || ln "$p" "$FEED/$b" 2>/dev/null || cp "$p" "$FEED/$b"; done
}
# versions_props: DotNetPackageVersionPropsPath for the packages of $FEED (source-build's PackageVersions.props)
versions_props() { python3 -S "$M612_TOOLS_SRC/s12versions.py" "$FEED" > "$VERPROPS.tmp" && mv "$VERPROPS.tmp" "$VERPROPS"; }

# MSBuild properties of every S12 repo build: the upstream source-build configuration
# (DotNetBuildFromSource, Product flavour), deterministic CI build with /_/ source paths (Arcade sets
# SourceRoot=RepoRoot when SCM queries are off; the trees are tarballs, not git checkouts), the
# lifted versions, and the target frameworks the SDK needs (net8.0; netstandard2.0 for analyzers,
# generators and libraries that ship it)
S12_TFMS=${S12_TFMS:-netstandard2.0%3bnet8.0}
s12_props() {
  echo -p:DotNetBuildFromSource=true -p:DotNetBuildFromSourceFlavor=Product \
       -p:ContinuousIntegrationBuild=true -p:Deterministic=true \
       -p:EnableSourceControlManagerQueries=false -p:EnableSourceLink=false -p:PublishWindowsPdb=false -p:FullAssemblySigningSupported=false \
       -p:UsingToolPdbConverter=false -p:UsingToolVSSDK=false -p:UsingToolIbcOptimization=false \
       -p:UsingToolVisualStudioIbcTraining=false -p:UsingToolSymbolUploader=false \
       -p:DotNetTargetFrameworkFilter="$S12_TFMS" \
       -p:DotNetPackageVersionPropsPath="$(w "$VERPROPS")" $(s12_kf)
}
# the older targeting packs for an SDK that does not list them (s12/KnownFrameworks.targets)
s12_kf() {
  local pm=""
  if ! ls "$BUILD_SDK"/packs/Microsoft.NETCore.App.Ref/*/data/PlatformManifest.txt > /dev/null 2>&1; then
    [ -f "$SN/PlatformManifest.txt" ] || python3 -S - "$(ls "$BUILD_SDK"/packs/Microsoft.NETCore.App.Ref/*/data/FrameworkList.xml | head -1)" "$SN/PlatformManifest.txt" <<'EOPY'
import re, sys
s = open(sys.argv[1], encoding='utf-8-sig').read()
out = []
for m in re.finditer(r'<File [^>]*Path="([^"]+)"[^>]*AssemblyVersion="([^"]+)"[^>]*FileVersion="([^"]+)"', s):
    out.append('%s|Microsoft.NETCore.App.Ref|%s|%s' % (m.group(1).split('/')[-1], m.group(2), m.group(3)))
open(sys.argv[2], 'w', newline='\n').write('\n'.join(sorted(out)) + '\n')
EOPY
    pm="-p:S12PlatformManifest=$(w "$SN/PlatformManifest.txt")"
  fi
  echo -p:CustomAfterMicrosoftCommonTargets="$(w "$S12_FILES/KnownFrameworks.targets")" $pm
}

# extract <tarball> <dir>: fresh tree, binaries deleted (provenance rule)
s12_extract() {
  local t=$1 d=$2
  rm -rf "${d:?}"; mkdir -p "$d"
  tar -C "$d" --strip-components=1 -xzf "$t"
  delete_binaries "$d"
}

# s12_restore <log> <tree> cmd...: run a restore; while it fails only for packages that
# source-build-reference-packages has, build those (s12-sbrp.sh refs) and try again
s12_restore() {
  local log=$1 tree=$2; shift 2; local i refs
  for i in 1 2 3 4 5 6 7 8; do
    if "$@" > "$log" 2>&1; then return 0; fi
    refs=$(python3 -S "$M612_TOOLS_SRC/s12missing.py" "$log" "$tree" "$S12/src/sbrp" 2> "$log.not-in-sbrp" | tr -d '\r' | sort -u)
    if [ -s "$log.not-in-sbrp" ]; then echo "packages that SBRP does not have:"; tr -d '\r' < "$log.not-in-sbrp" | sort -u; fi
    [ -n "$refs" ] || { grep -E ': error' "$log" | sed 's/ \[[A-Z]:.*//' | sort -u | head -40; return 1; }
    echo "restore: building from SBRP: $(echo $refs)"
    REFS="$refs" bash "$(dirname "${BASH_SOURCE[0]}")/s12-sbrp.sh" refs || return 1
  done
  return 1
}
# repo_commit <tarball>: the commit a GitHub tarball was made from (git get-tar-commit-id)
repo_commit() {
  local c; c=$( { set +o pipefail; gzip -dc "$1" 2>/dev/null | git get-tar-commit-id 2>/dev/null; } || true)
  echo "${c:-0000000000000000000000000000000000000000}"
}

# s12_build <name> <tree> <target(s)> [msbuild args...]: the standard S12 repo build in <tree>
# (global.json/NuGet.config already written): restore (with the SBRP retry loop), build, pack;
# logs in $S12/logs/<name>-s<N>-*.log + .binlog; every nupkg under artifacts/packages goes to the stage feed.
# <target(s)> is a project/solution/slnf path (relative to <tree>) or several separated by ';'.
s12_build() {
  local name=$1 tree=$2 targets=$3; shift 3
  local P=(-p:Configuration=Release -nologo -nodeReuse:false $(s12_props) "$@")
  local L=$S12/logs/$name-s$STAGE t0=$SECONDS t tgts
  IFS=';' read -r -a tgts <<< "$targets"
  cd "$tree"
  versions_props
  for t in "${tgts[@]}"; do
    s12_restore "$L-restore.log" "$tree" dn restore "$t" "${P[@]}" -v:m || die "$name: restore of $t failed ($L-restore.log)"
  done
  echo "TIME $name restore $((SECONDS - t0)) s"; t0=$SECONDS
  for t in "${tgts[@]}"; do
    dn build "$t" --no-restore "${P[@]}" -v:m -bl:"$(w "$L-$(basename "$t").binlog")" > "$L-build.log" 2>&1 \
      || { grep -E ': error' "$L-build.log" | sed 's/ \[[A-Z]:.*//' | sort -u | head -40; die "$name: build of $t failed ($L-build.log)"; }
  done
  echo "TIME $name build $((SECONDS - t0)) s"; t0=$SECONDS
  if [ -z "${S12_NOPACK:-}" ]; then
    for t in "${tgts[@]}"; do
      dn pack "$t" --no-build "${P[@]}" -v:m > "$L-pack.log" 2>&1 \
        || { grep -E ': error' "$L-pack.log" | sed 's/ \[[A-Z]:.*//' | sort -u | head -40; die "$name: pack of $t failed ($L-pack.log)"; }
    done
    echo "TIME $name pack $((SECONDS - t0)) s"
  fi
  local p; for p in $(find "$tree/artifacts/packages" -name '*.nupkg' ! -name '*.symbols.nupkg' 2>/dev/null); do feed_add "$p"; echo "feed: $(basename "$p")"; done
}
# s12_prep_repo <tarball> <tree> <patchdir> [MsbuildSdk=version...]: fresh tree, patches, global.json, NuGet.config
s12_prep_repo() {
  local tar=$1 tree=$2 pd=$3; shift 3
  s12_extract "$tar" "$tree"
  apply_patches "$tree" "$pd"
  [ -f "$tree/global.json" ] || echo '{}' > "$tree/global.json"
  python3 -S "$M612_TOOLS_SRC/s12globaljson.py" "$tree/global.json" 8.0.131 Microsoft.DotNet.Arcade.Sdk=$ARCADE_VER "$@"
  s12_nugetconfig "$tree"
}
