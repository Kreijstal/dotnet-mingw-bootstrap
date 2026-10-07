#!/bin/sh
# Refresh the copies of ../../patches/{treecc,pnet,pnetlib}/*.patch used by
# this PKGBUILD (makepkg only accepts local sources next to the PKGBUILD).
cd "$(dirname "$0")" || exit 1
rm -f treecc-0*.patch pnet-0*.patch pnetlib-0*.patch
for st in treecc pnet pnetlib; do
  for p in ../../patches/$st/*.patch; do cp "$p" "$st-$(basename "$p")"; done
done
cp ../tools/ln-s-rel ln-s-rel
