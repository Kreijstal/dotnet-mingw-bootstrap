# helpers shared by stage scripts
log() { printf '\n==> %s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
apply_patches() { # apply_patches <srcdir> <patchdir>
  local src=$1 pd=$2 p
  [ -d "$pd" ] || return 0
  for p in "$pd"/[0-9][0-9][0-9][0-9]-*.patch; do
    [ -e "$p" ] || continue
    if patch -d "$src" -p1 -N --dry-run -s -i "$p" >/dev/null 2>&1; then
      log "patch $(basename "$p")"; patch -d "$src" -p1 -N -s -i "$p" || die "patch $p"
    elif patch -d "$src" -p1 -R --dry-run -s -i "$p" >/dev/null 2>&1; then
      log "already applied: $(basename "$p")"
    else die "patch does not apply: $p"; fi
  done
}
# list managed/native binaries in a tree (provenance check)
scan_binaries() { find "$1" -path '*/.git' -prune -o -type f ! -iname 'COPYING*' \( -iname '*.dll' -o -iname '*.exe' -o -iname '*.so' -o -iname '*.nupkg' -o -iname '*.a' -o -iname '*.lib' -o -iname '*.o' -o -iname '*.obj' \) -print; }
