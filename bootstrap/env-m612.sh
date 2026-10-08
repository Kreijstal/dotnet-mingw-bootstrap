# Environment for phase 6 (Mono 6.12 seed -> Roslyn/MSBuild/.NET SDK).
# Source this; do not execute.
#
# POLICY (2026-10-08, see docs/bootstrap-plan.md "Policy change"): upstream
# MSYS2 packages are trusted, including mingw-w64-ucrt-x86_64-mono
# 6.12.0.199 (its csc is Roslyn 3.9).  That Mono is the seed of this phase,
# so unlike env.sh this environment deliberately does NOT put bootstrap/poison
# on PATH and lets /ucrt64/bin/mono, csc, resgen through.
#
# Everything above the seed must come from source: no Microsoft SDK/runtime
# zips, no prebuilt Roslyn/MSBuild/NuGet IL, no dotnet-install, no monolite.
#
# M612     build root of this phase (sources, build trees, stage prefixes)
# SEED_CSC the seed compiler (Roslyn 3.9 shipped in the MSYS2 mono package)
export DNB="${DNB:-$HOME/dotnetboot}"
export M612="${M612:-$DNB/m612}"
export MSYSTEM="${MSYSTEM:-UCRT64}"
case "$MSYSTEM" in
  UCRT64)  _mp=/ucrt64 ;;
  CLANG64) _mp=/clang64 ;;
  *) echo "env-m612.sh: unsupported MSYSTEM=$MSYSTEM" >&2; return 1 ;;
esac
export MINGW_PREFIX="$_mp"
export PATH="${BOOT_EXTRA_PATH:+$BOOT_EXTRA_PATH:}$_mp/bin:/usr/bin:/usr/bin/core_perl"
export MONO_LIB="$_mp/lib/mono/4.5"
export SEED_CSC="$MONO_LIB/csc.exe"
unset _mp
# Never let MSYS2 rewrite csc/msbuild style options; we only use '-' options
# and relative paths, but keep the usual exclusions for '/' style switches.
export MSYS2_ARG_CONV_EXCL="/nologo;/optimize;/debug;/unsafe;/nowarn:;/warn:;/r:;/reference:;/d:;/define:;/out:;/target:;/t:;/p:;/v:;/m:;/restore;/bl;/noconfig;/nostdlib;/langversion:;/property:;/verbosity:;/maxcpucount"
export DOTNET_CLI_TELEMETRY_OPTOUT=1
log() { printf '\n==> %s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
# m612_csc <csc.exe> args... : run a managed csc under the seed mono
m612_csc() { local c=$1; shift; mono "$c" -nologo "$@"; }
apply_patches() { # apply_patches <srcdir> <patchdir>   (stamped, incremental)
  local src=$1 pd=$2 p stamp
  [ -d "$pd" ] || return 0
  stamp="$src/.applied-patches"; touch "$stamp"
  for p in "$pd"/[0-9][0-9][0-9][0-9]-*.patch; do
    [ -e "$p" ] || continue
    grep -qxF "$(basename "$p")" "$stamp" && continue
    log "patch $(basename "$p")"
    patch -d "$src" -p1 -N -s -i "$(cd "$(dirname "$p")" && pwd)/$(basename "$p")" || die "patch does not apply: $p"
    basename "$p" >> "$stamp"
  done
}
scan_binaries() { find "$1" -path '*/.git' -prune -o -type f \( -iname '*.dll' -o -iname '*.exe' -o -iname '*.so' -o -iname '*.dylib' -o -iname '*.nupkg' -o -iname '*.a' -o -iname '*.lib' -o -iname '*.o' -o -iname '*.obj' -o -iname '*.winmd' -o -iname '*.pdb' -o -iname '*.resources' \) -print; }
