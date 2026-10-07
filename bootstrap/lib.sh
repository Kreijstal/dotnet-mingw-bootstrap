# helpers shared by stage scripts
log() { printf '\n==> %s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
apply_patches() { # apply_patches <srcdir> <patchdir>
  # Idempotent: applied patches are recorded in <srcdir>/.applied-patches, so a
  # failed build can be continued after adding a new patch (no rebuild).
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
# list managed/native binaries in a tree (provenance check)
scan_binaries() { find "$1" -path '*/.git' -prune -o -type f ! -iname 'COPYING*' \( -iname '*.dll' -o -iname '*.exe' -o -iname '*.so' -o -iname '*.nupkg' -o -iname '*.a' -o -iname '*.lib' -o -iname '*.o' -o -iname '*.obj' \) -print; }
