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
  # BCSC is csc.exe (a net472 build, run by the seed Mono) or csc.dll (a net8.0 build, run by
  # CSC_DOTNET, the source-built SDK's dotnet.exe)
  local runner=(mono)
  [[ $BCSC == *.dll ]] && runner=("${CSC_DOTNET:-$M612/dotnet-sdk-8.0.131-net8/dotnet.exe}" exec)
  "${runner[@]}" "$BCSC" -nologo -noconfig -langversion:${LANGVERSION:-preview} -nowarn:1701,1702,8632,8669,1573,1591,${EXTRA_NOWARN:-0} @"$rsp" > "$B/obj/$name.log" 2>&1 || rc=$?
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
fetch_tarball() { [ -f "$2" ] || die "package source closure missing: $2 ($1)"; }
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

# ---- net8.0 flavour of the tool builds (S11) ----
# Compile references for a net8.0 assembly: the reference pack (refpack.sh), the
# out-of-band libraries (libraries.sh OOB=...), then the given assemblies or
# directories; one reference per assembly name, the last one given wins.
# NET8_SELF names the assembly being compiled (never referenced).
NET8_RP=$M612/build/refpack-8.0.0/out
NET8_OOB=$M612/oob-8.0.0/lib
NET8_DEFS=(-d:TRACE -d:RELEASE -d:NETCOREAPP -d:NET -d:NET8_0 -d:NET8_0_OR_GREATER -d:NET7_0_OR_GREATER -d:NET6_0_OR_GREATER
  -d:NET5_0_OR_GREATER -d:NETCOREAPP3_1_OR_GREATER -d:NETCOREAPP3_0_OR_GREATER -d:NETCOREAPP2_2_OR_GREATER -d:NETCOREAPP2_1_OR_GREATER
  -d:NETCOREAPP2_0_OR_GREATER -d:NETCOREAPP1_1_OR_GREATER -d:NETCOREAPP1_0_OR_GREATER)
net8_refs() {
  local -A m; local f a n
  for f in "$NET8_RP"/*.dll "$NET8_OOB"/*.dll; do [ -e "$f" ] && m[$(basename "$f" .dll)]=$f; done
  for a in "$@"; do
    if [ -d "$a" ]; then for f in "$a"/*.dll; do [ -e "$f" ] && m[$(basename "$f" .dll)]=$f; done
    elif [ -e "$a" ]; then m[$(basename "$a" .dll)]=$a; fi
  done
  [ -n "${NET8_SELF:-}" ] && unset "m[$NET8_SELF]"
  for n in "${!m[@]}"; do echo "-r:$(winpath "${m[$n]}")"; done
}

# net8tool <name> [args...]: compile tools/<name>.cs (if needed) with the net8.0 csc.dll of the
# source-built SDK against the reference pack, and run it on that SDK's runtime
net8tool() {
  local name=$1; shift
  local dn=${CSC_DOTNET:-$M612/dotnet-sdk-8.0.131-net8/dotnet.exe} t=$M612/tools/$name.dll r refs=()
  if ! [ "$t" -nt "$M612_TOOLS_SRC/$name.cs" ]; then
    mkdir -p "$M612/tools"
    for r in System.Runtime System.Reflection.Metadata System.Collections.Immutable System.Console System.Memory \
             System.Linq System.IO System.Runtime.Extensions System.Collections; do
      [ -f "$NET8_RP/$r.dll" ] && refs+=(-r:"$(cygpath -m "$NET8_RP/$r.dll")")
    done
    "$dn" exec "$(cygpath -m "$M612/dotnet-sdk-8.0.131-net8/sdk/8.0.131/Roslyn/bincore/csc.dll")" -nologo -noconfig -nostdlib \
      "${refs[@]}" -out:"$(cygpath -m "$t")" "$(cygpath -m "$M612_TOOLS_SRC/$name.cs")" || return 1
    printf '{"runtimeOptions":{"tfm":"net8.0","framework":{"name":"Microsoft.NETCore.App","version":"8.0.0"}}}\n' > "${t%.dll}.runtimeconfig.json"
  fi
  "$dn" "$(cygpath -m "$t")" "$@"
}
