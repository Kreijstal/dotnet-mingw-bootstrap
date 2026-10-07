#!/bin/bash
# Port of the Guix origin snippet for pnet-git (gnu/packages/dotnet.scm,
# define-public pnet-git, introduced by Guix commit 1b1e9917ad04).
# Run inside a fresh pnet checkout (commit 3baf94734d8dc3fdabba68a8891e67a43ed6c4bd).
set -euo pipefail
# bundled copies of libffi and libgc: use the MSYS2 packages instead
rm -rf libffi libgc
# pregenerated autotools output and pregenerated parsers/scanners
rm -f compile configure config.guess config.sub depcomp install-sh ltconfig ltcf-c.sh ltmain.sh
find . \( -name Makefile -o -name Makefile.in \) -type f -delete
find . -type f \( -name '*_grammar.c' -o -name '*_grammar.h' -o -name '*_scanner.c' -o -name '*_scanner.h' \) -delete
perl -pi -e "s/GCLIBS='.*libgc.a'/GCLIBS='-lgc'/; s/search_libjit=true/search_libjit=false\nJIT_LIBS=-ljit/" configure.in
perl -ni -e 'print unless /OPT_SUBDIRS \+= lib/' Makefile.am
perl -pi -e 's|#include .*/libgc/include/gc.h.|#include <gc.h>|; s|#include .*/libgc/include/gc_typed.h.|#include <gc/gc_typed.h>|' support/hb_gc.c
for f in codegen/Makefile.am cscc/bf/Makefile.am cscc/csharp/Makefile.am cscc/c/Makefile.am cscc/java/Makefile.am; do
  perl -ni -e 'print unless /^TREECC_OUTPUT =/' "$f"   # generated files aren't prerequisites
done
perl -pi -e 's/YYLEX/yylex()/g' cscc/csharp/cs_grammar.y
perl -pi -e 's/^(CCPreProc CCPreProcessorStream;)/extern $1/' cscc/common/cc_main.h
perl -pi -e 's/^(int\ttoken;)/extern $1/' csdoc/scanner.c
perl -pi -e 's/python1\.5/python/' doc/cvmdoc.py
# unrollers are arch specific and segfault pnetlib's tests (Guix comment)
perl -pi -e 's/IL_CONFIG_UNROLL=y/IL_CONFIG_UNROLL=n/' profiles/full
