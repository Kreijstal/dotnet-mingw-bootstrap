#!/bin/bash
# Smoke test of an in-tree mono-1.2.6 build (run on win11):
#  1. the C runtime starts, 2. the cscc-built basic-profile mcs.exe runs on
#  the JIT, 3. it compiles and 4. runs a hello world.
# usage: MSYSTEM=UCRT64 bash mono126-smoke.sh [hello.cs]
. "$(dirname "$0")/../env.sh"
S=$DNB/build/mono-1.2.6-$MSYSTEM/mono-1.2.6
M=$S/mono/mini/.libs/mono.exe; [ -f "$M" ] || M=$S/mono/mini/mono.exe
L=$S/mcs/class/lib/basic
src=${1:-$DNB/hello.cs}
[ -f "$src" ] || printf 'class H { static void Main () { System.Console.WriteLine ("hello from mono-1.2.6"); } }\n' > "$src"
cd "$(dirname "$src")"
"$M" --version | head -1;                                   echo "runtime rc=$?"
MONO_PATH=$L "$M" "$L/mcs.exe" --version;                   echo "mcs --version rc=$?"
MONO_PATH=$L "$M" "$L/mcs.exe" /nologo -out:hello126.exe "$(basename "$src")"; echo "mcs compile rc=$?"
MONO_PATH=$L "$M" hello126.exe;                             echo "run rc=$?"
