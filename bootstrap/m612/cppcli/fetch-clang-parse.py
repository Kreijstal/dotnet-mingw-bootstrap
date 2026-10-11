#!/usr/bin/env python3
"""Fetch locked LLVM 22.1.8 parser sources and apply the CLI-only patch."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

here = Path(__file__).resolve().parent
cache, work = map(Path, sys.argv[1:3])
manifest = here / "clang-parse-manifest.json"
patch = here / "clang-caret.patch"
fingerprint = hashlib.sha256(manifest.read_bytes() + patch.read_bytes()).hexdigest()
stamp = work / ".prepared"
if stamp.is_file() and stamp.read_text().strip() == fingerprint:
    sys.exit(0)
cache.mkdir(parents=True, exist_ok=True)
work.mkdir(parents=True, exist_ok=True)
for entry in json.loads(manifest.read_text()):
    name, digest = entry["file"], entry["sha256"]
    cached = cache / name
    if not cached.is_file() or hashlib.sha256(cached.read_bytes()).hexdigest() != digest:
        url = "https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-22.1.8/clang/lib/Parse/" + name
        data = urllib.request.urlopen(url).read()
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit("Clang parser checksum mismatch: " + name)
        cached.write_bytes(data)
    shutil.copyfile(cached, work / name)
subprocess.run(["patch", "-s", "-p0", "-d", str(work.resolve()), "-i", str(patch)], check=True)
stamp.write_text(fingerprint + "\n")
