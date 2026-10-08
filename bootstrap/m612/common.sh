# Shared helpers for the m612 stage scripts.  Source after env-m612.sh.
# Needs: B (build dir), BCSC (compiler csc.exe used for this stage).
ML=$(cygpath -m "$MONO_LIB"); RA=$(cygpath -m "$MONO_LIB/../4.7.2-api")
KEYS=$(cd "$(dirname "${BASH_SOURCE[0]}")/keys" && pwd)

# csc <log-name> args... : compile via a response file (Windows' 32K command
# line limit), keep the full log in obj/<log-name>.log, show errors, fail on error.
MSYS_ROOT_W=$(cygpath -m /)
# winpath <path>: MSYS absolute path -> Windows path, without forking cygpath
winpath() {
  local p=$1
  case $p in
    /[a-zA-Z]/*) printf '%s:%s' "${p:1:1}" "${p:2}" ;;
    /*) printf '%s%s' "$MSYS_ROOT_W" "$p" ;;
    *) printf '%s' "$p" ;;
  esac
}
csc() {
  local name=$1; shift
  mkdir -p "$B/obj"; local rsp="$B/obj/$name.rsp" a opt
  : > "$rsp"
  for a in "$@"; do
    if [[ $a =~ ^(-[A-Za-z]+):(/.*)$ ]]; then a="${BASH_REMATCH[1]}:$(winpath "${BASH_REMATCH[2]}")"
    elif [[ $a == /* ]]; then a=$(winpath "$a"); fi
    printf '"%s"\n' "$a" >> "$rsp"
  done
  local rc=0
  mono "$BCSC" -nologo -noconfig -langversion:${LANGVERSION:-preview} -nowarn:1701,1702,8632,8669,1573,1591,${EXTRA_NOWARN:-0} @"$rsp" > "$B/obj/$name.log" 2>&1 || rc=$?
  grep -E 'error CS' "$B/obj/$name.log" | head -${SHOW_ERRORS:-60} || true
  echo "csc $name: rc=$rc, $(grep -c 'error CS' "$B/obj/$name.log" || true) errors"
  return $rc
}

# References: the .NET Framework 4.7.2 reference assemblies shipped by the
# MSYS2 mono package (lib/mono/4.7.2-api), like a real net472 build.  Mono's
# runtime mscorlib (lib/mono/4.5) has a public Span<T> of its own, which
# clashes with System.Memory's (CS0433/CS0012), so it is not used for
# compiling.
NETFX_REFS=(-r:"$RA/mscorlib.dll" -r:"$RA/System.dll" -r:"$RA/System.Core.dll"
  -r:"$RA/System.Xml.dll" -r:"$RA/System.Xml.Linq.dll" -r:"$RA/System.Numerics.dll"
  -r:"$RA/System.IO.Compression.dll" -r:"$RA/System.Runtime.Serialization.dll"
  -r:"$RA/System.Security.dll" -r:"$RA/System.Configuration.dll"
  -r:"$RA/Facades/netstandard.dll" -r:"$RA/Facades/System.Runtime.dll" -r:"$RA/Facades/System.ValueTuple.dll"
  -r:"$RA/Facades/System.Threading.Tasks.dll" -r:"$RA/Facades/System.Collections.dll")

# "Package" assemblies (what NuGet would supply for net472).  PKGDIR is a
# directory of from-source builds (see netfx-pkgs.sh); anything not found
# there falls back to the copy in the trusted Mono seed (lib/mono/4.5).
PKG_NAMES="System.Collections.Immutable System.Reflection.Metadata System.Memory System.Buffers
  System.Runtime.CompilerServices.Unsafe System.Threading.Tasks.Extensions System.Numerics.Vectors
  System.Text.Encoding.CodePages"
pkg_path() { # pkg_path <AssemblyName> -> windows path or nothing
  local n=$1
  if [ -n "${PKGDIR:-}" ] && [ -f "$PKGDIR/$n.dll" ]; then cygpath -m "$PKGDIR/$n.dll"
  elif [ -f "$MONO_LIB/$n.dll" ]; then echo "$ML/$n.dll"
  elif [ -f "$MONO_LIB/Facades/$n.dll" ]; then echo "$ML/Facades/$n.dll"
  fi
}
pkg_refs() { local n p; for n in ${*:-$PKG_NAMES}; do p=$(pkg_path "$n"); [ -n "$p" ] && echo "-r:$p"; done; }
pkg_runtime_files() { local n p; for n in ${*:-$PKG_NAMES}; do p=$(pkg_path "$n"); [ -n "$p" ] && echo "$p"; done; }

# Build-time tools (generators) target .NET Core upstream and only ever run
# here on Mono: compile them against Mono's runtime profile (lib/mono/4.5),
# which has Index/Range, string.Contains(char)...
TOOLREFS=(-r:"$ML/mscorlib.dll" -r:"$ML/System.dll" -r:"$ML/System.Core.dll" -r:"$ML/System.Xml.dll"
  -r:"$ML/System.Xml.Linq.dll" -r:"$ML/Facades/netstandard.dll" -r:"$ML/Facades/System.Runtime.dll"
  -r:"$ML/System.Collections.Immutable.dll")

# public-sign key file: keyfile <Microsoft|Open|MSShared>
keyfile() {
  local f; f=$(ls "$KEYS/$1"-*.pubkey.hex) || return 1
  local out="$M612/keys/$1.snk"; mkdir -p "$M612/keys"
  [ -f "$out" ] || perl -ne 'BEGIN { binmode STDOUT } s/[^0-9a-fA-F]//g; print pack("H*", $_)' "$f" > "$out"
  cygpath -m "$out"
}

# fetch_tarball <url> <file>
fetch_tarball() { [ -f "$2" ] || { log "fetch $1"; curl -fsSL -o "$2.part" "$1" && mv "$2.part" "$2"; }; }
# delete_binaries <tree> : provenance rule, no PE/archives/object files survive
delete_binaries() {
  scan_binaries "$1" > "$1/.deleted-binaries.txt" || true
  log "deleting $(wc -l < "$1/.deleted-binaries.txt") binary files from $(basename "$1")"
  tr '\n' '\0' < "$1/.deleted-binaries.txt" | xargs -0 -r rm -f
  [ -z "$(scan_binaries "$1")" ] || die "binaries left in $1"
}
resxgen() {
  if ! [ "$M612/tools/ResxGen.exe" -nt "$M612_TOOLS_SRC/ResxGen.cs" ]; then
    mkdir -p "$M612/tools"
    mono "$SEED_CSC" -nologo -out:"$M612/tools/ResxGen.exe" -r:System.Xml.dll -r:System.Core.dll "$M612_TOOLS_SRC/ResxGen.cs"
  fi
  mono "$M612/tools/ResxGen.exe" "$@"
}
M612_TOOLS_SRC=$(cd "$(dirname "${BASH_SOURCE[0]}")/tools" && pwd)
csproj_items() { python3 -S "$M612_TOOLS_SRC/csproj_items.py" "$@"; }
