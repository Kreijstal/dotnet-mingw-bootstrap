#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the Windows launcher's relocation, quoting, pipes and exit status."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

source, compiler, work = map(Path, sys.argv[1:])
work = work.resolve() / "launcher check λ"
bin_dir = work / "bin"
cwd = work / "working dir λ"
bin_dir.mkdir(parents=True, exist_ok=True)
cwd.mkdir(parents=True, exist_ok=True)
child = work / "child.py"
child.write_text("import json, os, sys\nprint(json.dumps([sys.argv[1:], os.getcwd(), sys.stdin.read()]))\nsys.exit(37)\n", encoding="utf-8")
# GNU ld cannot open non-ASCII output paths: build next to the test tree and
# copy the launcher in, which is how packaging installs it anyway.
build = work.parent / "launcher-check-build"
build.mkdir(exist_ok=True)
config = build / "config.h"
config.write_text('#define TARGET_RELATIVE L"' + os.path.relpath(sys.executable, bin_dir).replace('\\', '/') + '"\n#define TOOL_RELATIVE L"../child.py"\n', encoding="utf-8")
subprocess.run([str(compiler), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-municode", "-include", str(config), str(source), "-o", str(build / "launcher.exe")], check=True)
exe = bin_dir / "launcher.exe"
shutil.copyfile(build / "launcher.exe", exe)
args = ["", "two words", 'quoted "value"', "trailing\\", "λ 😀", "a\\\\\"b"]
result = subprocess.run([str(exe), *args], input="stdin marker\n", text=True, encoding="utf-8", capture_output=True, cwd=cwd)
assert result.returncode == 37, (result.returncode, result.stderr)
actual_args, actual_cwd, actual_input = json.loads(result.stdout)
assert actual_args == args, (actual_args, args)
assert Path(actual_cwd) == cwd, (actual_cwd, cwd)
assert actual_input == "stdin marker\n", repr(actual_input)
print("PASS launcher: relocated Unicode/space paths, quoted/empty arguments, pipes, cwd, exit status")
