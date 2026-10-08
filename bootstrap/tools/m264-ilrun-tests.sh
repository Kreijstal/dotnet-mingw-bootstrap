#!/bin/bash
# cscc/ilrun shortcut: run the cscc-built mono-2.6.4 mcs.exe on ilrun:
#   hello world, a C# 2 (no generics) program, and self-compilation.
. "$(dirname "$0")/../env.sh"
S=$DNB/probe264/mono-mono-2.6.4; M=$S/mcs/mcs
T=$DNB/probe264/t; mkdir -p "$T"; cd "$T"; rm -f *.exe
ILRUN="ilrun -C 16384"   # bigger method cache page: mcs' yyparse is >32 KB of IL
printf 'class H { static void Main () { System.Console.WriteLine ("hello via mcs-2.6.4 on ilrun"); } }\n' > h.cs
printf 'using System; using System.Collections; class I { static IEnumerable It(){ yield return 1; yield return 2; } delegate int D(int x); static void Main(){ int s=0; foreach(object o in It()) s+=(int)o; D d = delegate(int x){ return x+s; }; Console.WriteLine("c#2-without-generics ok " + d(10)); } }\n' > c2.cs
for p in h c2; do echo "== $p"; $ILRUN $M/mcs.exe $p.cs 2>&1 | grep -v "^\[\|^[0-9]* - " | tail -4; [ -f $p.exe ] && ilrun $p.exe; done
echo "== self-host"; cd $M
/usr/bin/time -f "self-compile on ilrun: %es %MKB" $ILRUN ./mcs.exe /codepage:65001 /nologo -d:NET_1_1 -d:ONLY_1_1 -d:BOOTSTRAP_WITH_OLDLIB -target:exe -out:mcs2.exe cs-parser.cs @mcs.exe.sources 2>&1 | grep -v "warning\|^\[\|^[0-9]* - " | tail -8
ls -la mcs2.exe 2>/dev/null && cd "$T" && rm -f h.exe && $ILRUN $M/mcs2.exe h.cs >/dev/null 2>&1 && echo -n "mcs2 (self-built) -> " && ilrun h.exe
