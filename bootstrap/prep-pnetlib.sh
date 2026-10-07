#!/bin/bash
# Port of the Guix origin snippet + 'disable-x11-tests' phase for pnetlib-git
# (gnu/packages/dotnet.scm, define-public pnetlib-git, Guix commit d65ac39c9130).
# Run inside a fresh pnetlib checkout (c3c12b8b0c65f5482d03d6a4865f7670e98baf4c).
set -euo pipefail
rm -f configure config.guess config.sub install-sh ltmain.sh
find . \( -name Makefile -o -name Makefile.in \) -type f -delete
for f in $(find tests -name Makefile.am); do
  perl -pi -e 's|^TESTS_ENVIRONMENT.*|LOG_COMPILER = \$(SHELL)\nAM_LOG_FLAGS = \$(top_builddir)/tools/run_test.sh \$(top_builddir)|' "$f"
done
perl -pi -e 's/en_US/en_US.utf8/' tools/run_test.sh.in
# Guix rewrites the clrwrap symlink in tools/wrapper.sh.in into a shell
# wrapper; we keep upstream's behaviour for now (wrappers are not needed to
# build mono-1.2.6) -- revisit if pnetlib's installed tools are used.
perl -pi -e 's/System\.Windows\.Forms//' tests/Makefile.am   # disable-x11-tests
