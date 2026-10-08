#!/bin/bash
# cscc/ilrun shortcut probe: compile mono-2.6.4's basic-profile components
# (mcs.exe, mscorlib, System, System.XML) directly with pnet's cscc, outside
# Mono's build system, to find language/API gaps quickly.
# usage: MSYSTEM=UCRT64 bash m264-cscc-probe.sh [component...]
. "$(dirname "$0")/../env.sh"; . "$(dirname "$0")/../lib.sh"
R=$(cd "$(dirname "$0")/../.." && pwd)
P=$DNB/probe264; S=$P/mono-mono-2.6.4
if [ ! -f "$P/.prepared" ]; then
  rm -rf "${P:?}"; mkdir -p "$P"; tar -C "$P" -xzf "$DNB/src/mono-2.6.4.tar.gz"
  find "$S" -type f -regextype posix-extended -regex '.*\.(dll|exe|DLL|EXE|so|prebuilt)$' -delete
  (cd "$S/mcs/jay" && gcc $BOOT_CFLAGS -w -DSKEL_DIRECTORY=\".\" -o jay.exe *.c)
  sed -e 's/@MONO_VERSION@/2.6.4/' "$S/mcs/build/common/Consts.cs.in" > "$S/mcs/build/common/Consts.cs"
  touch "$P/.prepared"
fi
apply_patches "$S" "$R/patches/mono-2.6.4"
CSFLAGS="/codepage:65001 /nologo /noconfig -d:NET_1_1 -d:ONLY_1_1 -d:BOOTSTRAP_WITH_OLDLIB"   # /noconfig: else cscc adds pnetlib refs
for c in "${@:-mcs}"; do
  case $c in
  mcs)   # (no /usr/bin/time wrapper: cscc under msys time failed randomly)
    cd "$S/mcs/mcs"; ../jay/jay.exe -ctv < ../jay/skeleton.cs cs-parser.jay > cs-parser.cs 2>/dev/null
    cscc $CSFLAGS -target:exe -out:mcs.exe cs-parser.cs @mcs.exe.sources > $P/mcs.log 2>&1; echo "mcs rc=$?" ;;
  corlib)
    cd "$S/mcs/class/corlib"
    cscc $CSFLAGS -nostdlib -unsafe -nowarn:612,618 -d:INSIDE_CORLIB -target:library -out:mscorlib.dll @corlib.dll.sources > $P/corlib.log 2>&1; echo "corlib rc=$?" ;;
  esac
done
