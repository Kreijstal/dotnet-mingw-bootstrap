#!/bin/bash
# Port of Guix' prepare-mono-source-0 / prepare-mono-source snippets
# (gnu/packages/dotnet.scm; introduced with mono-1.2.6 in Guix commit
# 7d96fa640a6b71af444493f745a93837a7ea3e70).  Run in the top of a mono tree.
# usage: prep-mono.sh [--level0]   (--level0 = prepare-mono-source-0 only,
#                                   which Guix uses from mono-5.0.1 on)
set -euo pipefail
rm -f configure
rm -rf libgc
# "just to be sure": every managed/native binary in the tree, including the
# monolite seed (mcs/class/lib/monolite/*) and docs/AgilityPack.dll
find . -type f -regextype posix-extended -regex '.*\.(dll|exe|DLL|EXE|so)$' -print -delete | sed 's/^/deleted blob: /'
# docs needs the deleted AgilityPack.dll; drop docs and libgc from SUBDIRS
perl -pi -e 'if (/^(|DIST_|MOONLIGHT_|MONOTOUCH_)SUBDIRS =/) { s/ docs//; s/ \$\(libgc_dir\)//; s/ libgc//; }' Makefile.am
[ "${1:-}" = --level0 ] && exit 0
for f in configure.in configure.ac; do [ -f $f ] && perl -pi -e 's/int f = isinf \(1\);/int f = isinf (1.0);/' $f; done
if [ -f mono/io-layer/processes.c ]; then
  perl -pi -e 's/#ifdef HAVE_SYS_MKDEV_H/#if 1/; s|sys/mkdev.h|sys/sysmacros.h|' mono/io-layer/processes.c
fi
[ -f mono/metadata/boehm-gc.c ] && perl -pi -e 's/GC_set_finalizer_notify_proc/GC_set_await_finalize_proc/; s/GC_toggleref_register_callback/GC_set_toggleref_func/' mono/metadata/boehm-gc.c
if [ -f mono/utils/mono-compiler.h ]; then
  perl -pi -e 's/static __thread gpointer x MONO_TLS_FAST/static __thread gpointer x __attribute__((used))/; s/#define MONO_TLS_FAST /#define MONO_TLS_FAST __attribute__((used)) /' mono/utils/mono-compiler.h
fi
[ -f mono/metadata/sgen-alloc.c ] && perl -pi -e 's/static __thread char \*\*tlab_next_addr/static __thread char **tlab_next_addr __attribute__((used))/' mono/metadata/sgen-alloc.c
exit 0
