#!/bin/bash
# cscc/ilrun shortcut: mono-2.6.4 mcs (cscc-built) compiling itself on ilrun;
# prints the first lines of any failure.
. "$(dirname "$0")/../env.sh"
M=$DNB/probe264/mono-mono-2.6.4/mcs/mcs; cd "$M"; rm -f mcs2.exe
/usr/bin/time -f "self-compile on ilrun: %es %MKB" ilrun -C 16384 ./mcs.exe /codepage:65001 /nologo -d:NET_1_1 -d:ONLY_1_1 -d:BOOTSTRAP_WITH_OLDLIB -target:exe -out:mcs2.exe cs-parser.cs @mcs.exe.sources > "$DNB/probe264/selfhost.log" 2>&1
grep -v "warning CS" "$DNB/probe264/selfhost.log" | grep -v "^\[\|^[0-9]* - " | head -${1:-14}
ls -la mcs2.exe 2>/dev/null
