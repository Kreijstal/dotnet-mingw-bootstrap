# Porting the Guix Mono chain to MSYS2 (Windows/MinGW-w64)

This document covers two things:

- historical Windows support of the first stages of the chain;
- the build attempts on the win11 host, with their exact errors and fixes.

All patches mentioned live in `patches/<stage>/`. Each one starts with a
header explaining why it exists. Stage scripts live in `bootstrap/`.

## Build host

| Item | Value |
|---|---|
| Host | `ssh topkek@win11`, Windows 10.0.26200 (Win 11), 4 cores |
| MSYS2 | msys2-runtime 3.6.10, environment **UCRT64** (x86_64, UCRT) |
| Compiler | `x86_64-w64-mingw32-gcc` 16.2.0 (Rev4, MSYS2); mingw-w64 headers/crt 14.0.0.r426 |
| Autotools | autoconf 2.73, automake 1.18.1, libtool 2.6.2, bison 3.8.2, flex 2.6.4 |
| Libraries | mingw-w64-ucrt-x86_64-gc 8.2.12 (MSYS2 builds it `--enable-threads=posix`), libffi 3.8.0, glib2 2.90.0, zlib |
| Build dir | `~/dotnetboot/{src,build,logs,stage0,stage1-UCRT64}`; the scripts are rsynced to `~/dotnetboot/repo` |

**Contamination hazard.** win11 has `mingw-w64-ucrt-x86_64-mono 6.12.0.199-3`
installed explicitly, which puts `/ucrt64/bin/{mono.exe,mcs,csc,xbuild,…}` on
PATH. MSYS2's recipe runs `make get-monolite-latest`, so that Mono is seeded
from Microsoft/Xamarin binaries. pnetlib's configure even falls back to `csc`
from PATH when it cannot find `cscc`. `bootstrap/env.sh` therefore puts
`bootstrap/poison/` (failing `mono`, `mcs`, `csc`, `xbuild`, `msbuild`,
`dotnet` … wrappers) on PATH after our own stage prefixes and before
`/ucrt64/bin`. A configure test or a Makefile that reaches for the host Mono
fails loudly with exit status 97. I did not uninstall the package (shared
host).

## Historical Windows support of the early stages

| Stage | Win32 story (from the sources) | Toolchain assumed | CRT | Threads/GC | Notes for MSYS2 |
|---|---|---|---|---|---|
| treecc 0.3.10 | `configure.in`: `*-*-mingw*\|*-*-cygwin*` → `CC="gcc -mno-cygwin"` | Cygwin gcc in MinGW mode | msvcrt | – | `-mno-cygwin` was removed in GCC 4.7 → patch |
| pnet (2011 git) | Native Win32 code paths (`IL_WIN32_NATIVE`, `USE_WIN32_FIND`, `support/*.c`); `--enable-cygwin` (default) vs `-mno-cygwin`; `*-*-mingw*` → `THREADS=win32`, `-lwsock32 -lole32`. `HACKING`: "Cygwin environment (Windows only)" | Cygwin or mingw32, 32-bit | msvcrt | win32 threads, `GC_WIN32_THREADS`; libgc bundled (deleted, Guix-style) | Builds on UCRT64 x86_64 with 2 small patches (LLP64 and a UCRT macro clash) |
| pnetlib | `cygpath -w` conversion of tool paths for csant | Cygwin | – | – | 1 patch (double `.exe`) |
| mono 1.2.6 (2007-12) | `PLATFORM_WIN32`; native io-layer replacement (Win32 API instead of `wapi`); `msvc/*.vcproj` for MSVC; configure forces `CC="gcc -mno-cygwin -g"` for non-cross builds | Cygwin gcc-3 `-mno-cygwin` (i386) or MSVC | msvcrt | `libgc_threads=win32`, `gc_default=included` with `--enable-win32-dllmain` | **Win64 JIT port "begun"** (ChangeLog 2007-03-15, 2007-10-26 Jonathan Chambers: "Begin Win64 port", MSVC intrinsics like `_AddressOfReturnAddress`); no Win64 unwind-info registration (`RtlInstallFunctionTableCallback` appears only in 2.4.2.3+) |
| mono 1.9.1 | same as 1.2.6 | same | msvcrt | same | ChangeLog 2008-04-23 Win64 stack-argument fixes came after this release |
| mono 2.4.2.3 / 2.6.4 | adds `-mms-bitfields -mwindows`; `--with-glib=embedded\|system` (eglib optional) | Cygwin gcc-3 `-mno-cygwin` | msvcrt | same | Win64 unwind info present (`exceptions-amd64.c`) |
| mono 2.11.4 / 3.0.12 | `AC_ERROR(Don't set CC. mono can only be compiled with gcc-3 from the 'gcc-core' package)`; `MINGW_CROSS_COMPILE` for cross builds; eglib only | Cygwin gcc-3, or a MinGW cross compiler | msvcrt | `with_tls=pthread` | Native MSYS2 builds need a configure patch (setting CC is refused) |
| mono 3.12.1 → 6.12 | `*-mingw*\|*-*-cygwin*` with no forced CC; `with_tls=pthread`; from 4.x on, WINVER 0x0600 | any MinGW gcc | msvcrt/ucrt | sgen from 4.9.0 | MSYS2 itself builds 6.12.0.199 for ucrt64/clang64 (5 small patches in `mingw-w64-mono`), but from a monolite binary seed |

None of these versions knew UCRT. Everything up to 3.x targeted msvcrt via
Cygwin's MinGW mode. UCRT only shows up as API differences in the headers
(the `timezone` macro, a declared `inet_pton`), not as a design problem.

## The attempt log

Format: package/version · compiler · configure invocation · dependencies ·
result · first error · root cause · patch/workaround · next blocker.

### A1 – treecc 0.3.10 (UCRT64) – **SOLVED**
- **Compiler:** GCC 16.2 with `-std=gnu17` (GCC 16 defaults to C23; the
  2007–2011 code is C89/C99, and Guix pins nothing because its GCC 14
  defaults to gnu17).
- **Configure:** `./configure --build=x86_64-w64-mingw32 --host=x86_64-w64-mingw32 --prefix=$STAGE0` after
  `autoreconf -fi`.
- **First error:** `configure: error: cannot guess build type`. The 2007
  `config.guess` does not know `MINGW64_NT-10.0`. Workaround: always pass
  the triplet (`BOOT_TRIPLET`).
- **Second error:** `C compiler cannot create executables`. Root cause:
  `CC="gcc -mno-cygwin"` is forced for mingw hosts.
  Fix: `patches/treecc/0001-configure-drop-mno-cygwin-on-mingw.patch`.
- **Result:** `treecc.exe` built and installed.

### A2 – pnet git 3baf9473 (UCRT64) – **SOLVED**
- **Prep:** `bootstrap/prep-pnet.sh`, a 1:1 port of the Guix snippet. It
  deletes bundled libgc/libffi, pregenerated autotools output and
  parsers/scanners, uses `-lgc`, and sets `IL_CONFIG_UNROLL=n`. Then the 4
  Guix patches and `autoreconf -vif`.
- **Configure:** `./configure --build/--host x86_64-w64-mingw32 --prefix=$STAGE0`,
  with `CFLAGS=$BOOT_CFLAGS -Wno-pointer-to-int-cast` (no libjit, so the CVM
  interpreter is used).
- **Error 1:** `support/time.c:169: error: function '__timezone' is
  initialized like a variable`. Root cause: the mingw-w64 `<time.h>`
  macro `timezone` → `(*__timezone())`.
  Fix: `patches/pnet/0005-ucrt-timezone-macro.patch`.
- **Error 2:** `make install` failed with `cp: cannot stat 'ilasm.1.gz'`.
  Root cause: MSYS2's `$(LN_S)` copies, and resolves the target relative to
  the cwd, not to the link. Workaround: `make install
  LN_S=bootstrap/tools/ln-s-rel`. This is a build-script helper, not a
  source patch.
- **Error 3** (found while building pnetlib): csant filesets came back empty
  or csant segfaulted. Root cause: LLP64. `_findfirst()` returns an
  `intptr_t` that was stored in a `long`.
  Fix: `patches/pnet/0006-win64-findfirst-intptr.patch`.
- **Result:** `cscc.exe`, `ilrun.exe`, `ilasm`, `csant`, `resgen`, `ilgac` …
  are installed. They link only to `libffi-8.dll`, `libgc-1.dll` and
  `libwinpthread-1.dll`.

### A3 – pnetlib git c3c12b8b (UCRT64) – **SOLVED**
- **Prep:** `bootstrap/prep-pnetlib.sh` (Guix snippet and the
  `disable-x11-tests` phase), `autoreconf -vif`, configure `--without-x`.
- **Error:** csant was invoked with `cscc.exe.exe`, `resgen.exe.exe` and so
  on. Root cause: configure appends `$(EXEEXT)` to `AC_PATH_PROG` results,
  which already contain `.exe` in modern autoconf.
  Fix: `patches/pnetlib/0001-csant-flags-no-double-exeext.patch`. It must be
  applied *before* autoreconf; the script does that now.
- **Result:** 48 files in `stage0/lib/cscc/lib`, including `mscorlib.dll`,
  `System.dll`, `System.Xml.dll` and `System.Windows.Forms.dll`. Build time
  was about 2.5 min.
- **Smoke test:** `cscc -o h.exe h.cs && ilrun h.exe` prints `hello from
  cscc, sum=6`. **This gives a working C#-compiler + CLI runtime on
  Windows x86_64, built only from C sources with MSYS2 GCC.**

### A4 – mono 1.2.6 (UCRT64, x86_64) – **REQUIRES PATCHING** (in progress)
- **Source:** release tarball, sha256 verified against Guix. The 4 monolite
  blobs are deleted by `prep-mono.sh` (a port of Guix'
  `prepare-mono-source`).
- **Configure:** `./configure --build/--host x86_64-w64-mingw32
  --prefix=$DNB/stage1-UCRT64 --with-gc=boehm CFLAGS="$BOOT_CFLAGS
  -DARG_MAX=500"` after `autoreconf -fi` (the tarball has no `autogen.sh`).
  Configure picks `PLATFORM_WIN32`, `arch_target=amd64`, `HAVE_BOEHM_GC`
  (MSYS2 `-lgc`) and no `__thread` (`HAVE_KW_THREAD` undefined).
- **Make:** `make EXTERNAL_MCS=$STAGE0/bin/cscc
  EXTERNAL_RUNTIME=$STAGE0/bin/ilrun`, serial (as Guix does).
- **Errors in order, with fixes:**
  1. configure: `-mno-cygwin` forced. Fix:
     `0003-configure-native-mingw-no-mno-cygwin`.
  2. `ddk/ntddk.h: fatal error: wdm.h: No such file`. Fix:
     `0004-io-layer-mingw-w64-winternl`.
  3. `socket-io.c: static declaration of 'inet_pton' follows non-static
     declaration`. Fix: `0005-socket-io-use-ws2-inet_pton`.
  4. `mono-profiler-aot.c: too many arguments to function 'mkdir'`. Fix:
     `0006-profiler-aot-include-config`.
  5. After these, **the whole C runtime builds** (`mono.exe`, x86_64, static
     libmono, about 8.5 MB). `mono --version` reports `Architecture: amd64,
     GC: System Boehm (with typed GC)`.
  6. cscc reported `E:/msys64/nologo: No such file or directory`. Root
     cause: the MSYS2 runtime rewrites csc-style `/nologo`, `/optimize`,
     `/debug+` into Windows paths when it spawns native executables.
     Workaround: `MSYS2_ARG_CONV_EXCL` with the csc option prefixes, in
     `env.sh`.
  7. **cscc then compiles mono's basic-profile `mcs.exe`, `mscorlib.dll`
     and `System.dll`.** The first managed code that runs on the new JIT
     (mcs compiling System.Xml) stops with
     `ERROR:tramp-amd64.c:79:mono_arch_patch_callsite: assertion failed:
     ((((guint64)(addr)) >> 32) == 0)`. Root cause: there is no `MAP_32BIT`
     on Windows; JIT code and the images are above 4 GB. Fix:
     `0007-amd64-win64-nomap32bit`, which enables mono's existing
     FreeBSD/Solaris mode.
  8. A segfault at a heap address (`rip=0x804070`). Root cause: DEP. Dynamic
     methods were `malloc()`ed and the mprotect was skipped on Win32. Fix:
     `0008-codeman-win32-dep-dynamic-code`.
  9. A return into garbage after `System.Object:.ctor`. Root cause: Win64
     home space. Helper calls had no 32-byte home space, and
     `get_call_info` bumped `stack_usage` to 32 with no matching `sub`. Fixes:
     `0009-amd64-win64-home-space` (reserve at the frame bottom, no
     omit-fp) and `0010-amd64-win64-call-home-space-backport` (upstream
     a90eff17bc09/3a36e7b52026, svn r101545/r101861, 2008-04).
  10. `#GP` on `jmp *%rax` in the first C# `switch`. Root cause: **LLP64**.
      `amd64_is_imm32()` casts to `glong` (32-bit on Win64), so every
      imm32 test passes, and the switch lowering used a `(long)` magic
      offset. Fixes: `0011-switch-jump-table-llp64` and
      `0012-amd64-is-imm32-llp64`.
- **Current state** (`bootstrap/tools/mono126-smoke.sh`):
  - `mono mcs.exe --version` prints **"Mono C# compiler version 1.2.6.0"**:
    the cscc-built compiler runs on the x86_64 Windows JIT.
  - **Next blocker:** compiling a hello world crashes in
    `Mono.CSharp.Location:AddFile` on `source_files.Contains(path)`. It is a
    virtual call through `vtable+0x170` (slot 37) on a
    `System.Collections.Hashtable` whose `klass->vtable_size` is 34, so the
    slot is NULL (gdb). The same binaries work on Linux under Guix, so this
    is almost certainly another LLP64/Win64 issue in method/vtable
    resolution. I have not root-caused it yet.
  - gdb data for the next session, read at the crash: in the runtime's
    `Hashtable` class, `vtable_size`=34 and
    `vt[30..33]`=`KeyEquals`, `ContainsKey`, `ContainsValue`,
    `OnDeserialization`. Methods with `slot`>=30 are `KeyEquals`(30),
    `get_IsFixedSize`(31, flags 0x9c6), `get_IsReadOnly`(32, 0x9c6),
    `ContainsKey`(31), `ContainsValue`(32) and `OnDeserialization`(33), so
    slots 31/32 appear twice. `Contains` is not among them, yet the call site
    (in mcs.exe, compiled by cscc against *pnetlib's* mscorlib) uses slot 37.
  - Next step: run the same cscc-built mcs.exe under Guix' (Linux)
    mono-1.2.6 and compare `klass->vtable` and `cmethod->slot` for this call.
    That tells us whether this is LLP64 in `class.c`/`loader.c` or a
    MemberRef resolution difference.
- **Expected further work for x86_64:** more LLP64 audit of mono/mini and
  mono/metadata, Win64 SEH (no unwind info for JIT code; 1.2.6 uses only
  `SetUnhandledExceptionFilter`, which matters once managed exceptions or
  null-reference traps occur), xmm6–15 callee-saved
  (`MONO_ARCH_CALLEE_FREGS 0xef` uses xmm6/7 as scratch), the
  `CALLCONV_IS_STDCALL` "callee pops" assumption on Win64, and GC thread
  registration with MSYS2's pthreads-built libgc. Mono's own 2007 Windows
  build used libgc with win32 threads and DllMain registration;
  `bootstrap/00-bdwgc.sh` builds that variant.

### A5 – i686 track (MINGW32): stage 0 and mono 1.2.6 – **SOLVED**
- **Rationale:** in 2007 the only supported Windows target of Mono was
  32-bit x86, and its JIT/SEH code was used in production there. Managed
  assemblies are architecture-neutral, so a 32-bit chain can later hand a
  compiler and class libraries to an x86_64 build of a newer Mono (the
  WoW64 i686 runtime runs on the same machine).
- **Concern:** the MSYS2 `mingw32` repository is being run down. It has 230
  packages versus 3909 in `ucrt64`, and there is no `gc` or `libatomic_ops`.
  Installed from the repo: `mingw-w64-i686-gcc` 16.2.0, `-glib2` 2.90.0,
  `-pkgconf`, `-libffi`.
- **Stage 0- (bdwgc 8.2.12):** `bootstrap/00-bdwgc.sh`, from the release
  tarball (sha256 = MSYS2's `mingw-w64-gc`), built with
  `--enable-threads=win32 --enable-win32-dllmain --with-libatomic-ops=none`
  into `stage0-MINGW32`. This is the threads model Mono's 2007 Windows build
  used. Result: OK.
- **Stage 0 (treecc, pnet, pnetlib):** `MSYSTEM=MINGW32 bootstrap/stage0-all.sh`
  with the **same patches** as UCRT64 and no new ones. `cscc`/`ilrun`
  (PE32 i386) print `hello from cscc, sum=6`.
- **mono 1.2.6:** `MSYSTEM=MINGW32 bootstrap/04-mono-1.2.6.sh`
  (`--build/--host i686-w64-mingw32 --with-gc=boehm` against the private
  bdwgc), with these patches:
  - **Error 1:** `process.c: 'GetProcessId' redeclared as different kind of
    symbol`. configure's probe cannot link the stdcall-decorated
    `_GetProcessId@4`. Fix: `0013-process-getprocessid-mingw-w64`.
  - **Error 2:** `make install` failed with `gacutil.exe` printing its usage.
    The MSYS2 arg conversion rewrote gacutil's `/i /f`. Fix: added to
    `MSYS2_ARG_CONV_EXCL` in `env.sh`.
  - **Not needed on i686:** the amd64-only patches (0007, 0009–0012). They
    are no-ops there.
  - **Result:** `make` and `make install` succeed (117 MB in
    `~/dotnetboot/stage1-MINGW32`). All profiles are built: basic,
    net_1_1_bootstrap, default (1.1), net_2_0_bootstrap, net_2_0 (154 and
    162 assemblies), signed with the in-tree `sn.exe`. The installed `mcs`
    compiles and `mono` runs a hello world. The installed `gmcs` compiles a
    generic `List<int>` program, which prints `gmcs generics ok, sum=30 on
    Microsoft Windows NT 10.0.26200.0`.
- **This is the first usable Mono bootstrap stage built on MSYS2 entirely
  from source.** It contains only GCC-built C code (pnet, mono runtime) and
  managed code compiled by cscc → mcs. No .NET/Mono binary was used.

### A6 – mono 1.9.1 (MINGW32) – **SOLVED** (with the resources caveat below)
- **Source:** `github.com/mono/mono/archive/refs/tags/mono-1.9.1.1.tar.gz`
  (sha256 `7cc4d6a2…de1461`). I extracted it, ran `git add -A && git
  write-tree`, and got `b6fe3b24959762bdac992d09ecbf0c7f845c83e5`, which is
  exactly the tree of tag `mono-1.9.1.1`, the commit Guix uses.
- **Patches:** the 3 Guix patches, plus 6 MinGW patches rebased from 1.2.6
  (`patches/mono-1.9.1/0004–0009`). All applied cleanly.
- **Build:** `bootstrap/05-mono-1.9.1.sh` uses the Guix phases
  (`MONO_CREATE_IMAGE_VERSION=v1.1.4322`, `NO_SIGN_ASSEMBLY=yes`,
  disable-signing, delete-mdb). Compiler: stage-1 mono-1.2.6 on PATH.
- **Result:** builds and installs on the first try into `stage2-MINGW32`
  with profiles 1.0, 2.0, 2.1 and 3.5, and 140 successful compilations.
  The installed `mcs` and `gmcs` compile, and `mono` runs hello world and
  `List<int>` generics.

### Binary-provenance finding: prebuilt `.resources` (must be fixed before packaging)
- **Sources:** both mono-1.2.6 and 1.9.1 ship
  `mcs/class/Managed.Windows.Forms/resources/{System.Windows.Forms,System.Windows.Forms.en,System.Windows.Forms.de,keyboards}.resources.prebuilt`.
- **The fallback:** the Makefile rule is `$(RESGEN) $< || cp $@.prebuilt
  $@`. On MSYS2, resgen cannot load `System.Windows.Forms, Version=1.0.5000.0`
  for ResX support (`Error: Cannot load support for ResX format`), so **both
  MINGW32 test builds silently used the 4 blobs**. I checked with `cmp`:
  the built `.resources` files are identical to `.prebuilt`.
- **Guix:** its snippet deletes only `*.dll|*.exe|*.so`, so Guix keeps these
  files too. I did not verify whether its Linux builds fall back to them.
- **The `.resx` sources are not clean either.** They embed opaque data:
  base64 `BinaryFormatter` blobs such as `keyboard_table` (a serialized
  `KeyboardLayout[]`) and serialized bitmaps/icons. These are data, not
  code, but they still need a provenance decision.
- **Action taken:** `prep-mono.sh` now deletes `*.prebuilt` by default.
  `ALLOW_PREBUILT_RESOURCES=1` keeps them, for experiments only. The
  current `stage1-MINGW32`/`stage2-MINGW32` installs contain a
  System.Windows.Forms built from the blobs. That only affects SWF, which no
  compiler stage uses, but those installs must not be packaged as they
  are.
- **Planned fix**, any of:
  - a SWF-free ResX compiler (System.Xml + `ResourceWriter.AddResourceData`,
    which exists in 1.2.6's corlib) that turns `.resx` into `.resources`;
  - regenerating `keyboard_table` from its C# source;
  - dropping Managed.Windows.Forms from the bootstrap-only stages.

## cscc/ilrun shortcut (skipping the old Mono JITs)

The question: can pnet's C-implemented `cscc` (with `ilrun`) produce a
*newer* Mono C# compiler directly, so that the chain skips mono
1.2.6/1.9.1/2.4.2.3 and the i686 detour? All experiments ran on UCRT64
x86_64 with the existing stage 0, extended incrementally with the patches
listed below.

### S1 – What C# does cscc (pnet git 3baf947) accept?
I probed it with small programs (`bootstrap/tools`; the matrix was run on
win11):

| Feature | cscc |
|---|---|
| C# 1 (classes, structs, delegates, events, unsafe, `fixed`, properties) | yes |
| static classes, partial classes, different accessor accessibility | yes |
| generic type *declarations* (`class G<T>`), fields of generic type | yes |
| generic types in local declarations / `new G<int>()` / generic method calls | **no** (parse error at `<`) |
| iterators (`yield`) | **no** ("not yet supported") |
| anonymous methods | **no** ("not yet supported") |
| nullable `T?`, `??`, `global::` | **no** |
| lambdas, extension methods, auto-properties, object initializers (C# 3) | **no** |

So cscc is C# 1 plus fragments of C# 2. That is enough for mcs sources up to
Mono 2.6.x, which are written in C# 1 so that Mono's own 1.1 profile could
build them. I counted occurrences in `mcs/mcs/*.cs`:

| Mono | uses of `List<`/`Dictionary<`… | lambda/`var` | basic profile |
|---|---|---|---|
| 1.9.1, 2.4.2.3, 2.6.4 | 5–6 (comments/strings) | 7–15 | `-d:ONLY_1_1` (C# 1) |
| 2.8, 2.8.2 | 484 | – | `-d:NET_2_0 -d:BOOTSTRAP_BASIC` |
| 2.10.11 | 513 | – | NET_2_0 |
| 2.11.4 / 3.0.12 | 563 / 583 | 1414 / 1477 | NET_2_0 |

**Highest mcs that cscc can compile: Mono 2.6.4**, the last release whose
`basic` profile is C# 1.0/ONLY_1_1. From 2.8 on, both the compiler sources
and the basic profile need generics.

### S2 – cscc compiling mono-2.6.4's mcs.exe and basic corlib
`bootstrap/tools/m264-cscc-probe.sh` compiles the components outside
Mono's build system. Results:

- **mcs.exe:** compiles in about 1.9 s. It needed these cscc patches:
  - `pnet/0008-csc-codepage-65001-is-utf8`: with `/codepage:65001`, the UTF-8
    BOM made the parse fail;
  - `pnet/0009-assembly-version-2-3-parts`: `[AssemblyVersion("2.6.4")]`
    was rejected;
  - `pnet/0010-using-value-type-dispose`: `using` with a struct produced
    invalid IL, a cscc TODO;
  - plus one mcs source patch, `mono-2.6.4/0003-mcs-tokenizer-cscc-fallthrough`
    (cscc's reachability analysis).
- **mscorlib (basic):** compiles in about 1.2 s.
  - The build must pass `/noconfig`, as Mono's makefiles do; otherwise cscc
    also references pnetlib's mscorlib and every override fails.
  - Source patches: `0004-corlib-object-cscc-basic` (System.Object without
    `[Serializable]`, explicit Finalize, like Guix' 1.2.6 patch) and
    `0005-corlib-cscc-basic-misc` (explicit `IMethodMessage.Uri`, braces
    around two `fixed` statements).
- **System (basic):** cscc's missing anonymous methods, iterators and
  `[return:]` support were worked around in `0015-system-basic-cscc`
  (Guix-style rewrites, `#if BOOTSTRAP_WITH_OLDLIB` only).
- **A failed idea:** I first relaxed cscc's "circularity detected in class
  definition" check instead of patching System.Object. It broke the
  semantic-analysis order, so I reverted it. It is not in `patches/`.
- **Windows quirk:** running cscc under MSYS2's `/usr/bin/time` made it fail
  randomly without a message. The scripts don't use it.

### S3 – the cscc-built mcs running on ilrun
- **mcs 1.2.6** (Guix-patched, built by cscc) on `ilrun -C 16384`: `--version`
  works, and **hello world compiles and runs**.
  - `-C` (method cache page) is needed because `CSharpParser::yyparse` is
    bigger than the default 128 KB conversion buffer.
  - Fixes needed: `pnetlib/0002-typebuilder-null-parent-is-object` (mcs
    calls `DefineType(..., null)`, which pnet answered with OutOfMemory) and
    `pnet/0007-win32-getfiletype-getfileattributes` (`Directory.Exists` of
    `GetFullPath(".")` was false).
- **mcs 2.6.4** on ilrun: **hello world compiles and runs** in about 0.1 s.
  Self-hosting (mcs compiling itself on ilrun) gets further after
  `pnetlib/0003-typebuilder-enum-underlying-unset` and
  `pnetlib/0004-typebuilder-stable-hashcode`.
  - **It then stops on pnetlib's incomplete Reflection.Emit:**
    `ModuleBuilder.GetType(string)` is a `// TODO return null`, so mcs
    cannot form array types of TypeBuilders ("Couldn't create computed type
    TypeParameter[]"). There is no `SymbolType` equivalent. Generic
    Reflection.Emit (`DefineGenericParameters`, `MakeGenericType`), which
    gmcs needs, is likewise not there.
  - iterators/anonymous methods compiled by the basic mcs fail too, because
    that mcs wants generic corlib types for them.
- **Conclusion:** cscc-built mcs on ilrun is a working small C# 1 compiler,
  but it is **not** a usable route to compile big code (itself, or a 2.0
  corlib) without implementing the missing parts of pnetlib's
  Reflection.Emit.

### S4 – what it buys: a native mono-2.6.4 directly from cscc
`bootstrap/06-mono-2.6.4-cscc.sh`:
- `EXTERNAL_MCS=cscc EXTERNAL_RUNTIME="ilrun -C 16384"`.
- `SKIP_BASIC_PROFILE_CHECK=yes`, via `0012-basic-profile-check-skip-knob`.
  The check needs a corlib *without* CompilerGeneratedAttribute, and
  pnetlib is 2.0-flavoured.
- ilrun runs nothing else in Mono's build: from the second profile on,
  Mono's own freshly built runtime runs the cscc-built `basic/mcs.exe`. So
  **the interpreter's speed does not matter**. cscc itself is native C and
  compiles mcs or corlib in 1–2 s.

**On UCRT64 (x86_64): REQUIRES PATCHING.**
- The 2.6.4 runtime builds after the MinGW patches carried over from 1.2.6
  (`0006`–`0011`) plus:
  - `0013-coree-imagebase-mingw-w64` (`_image_base__` does not exist on
    x86_64);
  - `0014-mini-h-sys-types` (`pid_t`);
  - a static win32-threads bdwgc with `GC_DllMain`. Mono 2.x's `DllMain`
    calls `GC_DllMain`, and MSYS2's `gc` is built with pthreads. So
    `00-bdwgc.sh` gets `GC_PREFIX=$DNB/gc-win32-UCRT64 GC_STATIC=1`, which
    means `--disable-thread-local-alloc` and `-DGC_INSIDE_DLL`, the only
    combination that compiles `GC_DllMain`.
- The cscc-built basic profile (mcs.exe, mscorlib, System) is produced.
- **First blocker:** Mono 2.6.4's Win64 JIT. Any program, even a cscc-built
  "hello world" against the new basic mscorlib, exits with status 127
  without output. Console initialisation fails to load I18N, and the
  managed exception path then crashes in JIT code (`rcx=0`, no native frames
  found on the stack). An explicit `throw`/`catch` test segfaults. Mono 2.6
  did not support 64-bit Windows: Win64 was still MSVC-only work in
  progress.

**On MINGW32 (i686): REQUIRES PATCHING.**
- Stage 0 was rebuilt incrementally with the new pnet/pnetlib patches.
- Extra fixes needed:
  - `0016-configure-winver-vista`: with WINVER 0x0500 the inet_pton /
    GetProcessId prototypes are hidden, so i686 calls them cdecl and the
    link fails;
  - `00-bdwgc.sh` now deletes the static copy's `.la` files: libtool
    refused to link `libgc.la` into the libmono DLL and silently dropped
    `-lgc` ("This system cannot link to static lib archive").
- The runtime then builds, and cscc builds the basic mcs.exe, mscorlib and
  System.
- **First blocker:** the produced `libmono.dll` fails to initialise.
  `mono.exe` exits with status 127 even for `--version`, and
  `LoadLibrary("libmono.dll")` returns ERROR_DLL_INIT_FAILED (1114).
  bdwgc's `GC_DllMain` always returns TRUE, so the failure is elsewhere in
  the DLL start-up (CRT/TLS init of the C++-linked DLL, or the static GC
  inside a DLL). Not root-caused yet; there is no i686 gdb on win11.
  mono-1.2.6/1.9.1 on i686 are not affected: they are not built as a
  libmono DLL calling `GC_DllMain`.

### Result of the shortcut
- **Proven:**
  - the C-only root (cscc) can directly compile the *Mono 2.6.4* compiler
    and its basic-profile corlib/System, skipping Mono 1.2.6, 1.9.1 and
    2.4.2.3. That needs 9 small source/build patches on 2.6.4 and 4 cscc
    fixes;
  - the cscc-built mcs even runs on ilrun for small programs.
- **Not yet proven:** that a native mono-2.6.4 can run that basic profile on
  Windows.
  - x86_64: Mono 2.6's Win64 JIT crashes on the first managed exception.
  - i686: the libmono DLL does not initialise.

  Fixing either one would make cscc → mono-2.6.4 a 3-stage shortcut.
- **Timing:** cscc is native C and compiles mcs.exe in about 1.9 s and the
  basic corlib in about 1.2 s. ilrun runs a hello-world compile in about
  0.1–0.2 s. ilrun is not on the critical path of Mono's build, so
  interpreter speed is irrelevant for this route.

## i686 libmono init failure (mono-2.6.4 built by cscc, MINGW32)

**Symptom.** `mono.exe --version` exited with status 127 and printed
nothing. `LoadLibrary("libmono.dll")` failed with ERROR_DLL_INIT_FAILED
(1114).

**Tool.** I installed `mingw-w64-i686-gdb` 18.1-3 from the mingw32
repository (a source-built MSYS2 debugger).

**Evidence.**
- With a breakpoint on `GC_DllMain@12`, the backtrace is
  `ntdll → DllMainCRTStartup@12 → DllMain@12 (driver.c:1212) →
  GC_DllMain@12`. On return, the process faults at `0x005ffd24`, which is
  exactly the value of the `reserved` argument.
- objdump of libmono's `_DllMain@12` shows a cdecl-style call: arguments are
  stored with `mov` at `0..8(%esp)`, then `call _GC_DllMain`, with no stack
  adjustment afterwards, while `GC_DllMain@12` ends in `ret $0xc`.

**Root cause.**
- With an external bdwgc 8.x, `<gc.h>` declares `GC_DllMain` only when
  `GC_INSIDE_DLL` is defined, i.e. when compiling the GC itself.
- So `driver.c` called an *implicitly declared* (cdecl) `GC_DllMain`. That
  was only a warning, because the GCC 14+ escape hatch
  `-Wno-error=implicit-function-declaration` is in our CFLAGS. ld's stdcall
  fixup then resolved `_GC_DllMain` to the WINAPI `_GC_DllMain@12`.
- The callee popped 12 bytes, so libmono's DllMain returned through a
  corrupted frame.
- x86_64 has a single calling convention, so UCRT64 never saw this. Mono's
  bundled libgc used to declare the function unconditionally.

**Fix.** `patches/mono-2.6.4/0017-driver-gc-dllmain-prototype` declares
`BOOL WINAPI GC_DllMain (HINSTANCE, ULONG, LPVOID)`.
- After a relink (only `driver.c` recompiles), `mono --version` works.

**Next failure: stack overflow in `System.MulticastDelegate:op_Equality`.**
- Every program died there; the method called itself (gdb plus `mono -v`).
- cscc compiles `d1 == null` inside `operator ==` as a call to that same
  operator.
- Fix: `0018-corlib-multicastdelegate-op-eq-cscc` (`(object)` casts, as in
  Guix' 1.2.6 patch). Only the cscc-built basic mscorlib is rebuilt (cscc,
  about 5 s).

**Result.**
- On i686 mono-2.6.4 a cscc-built hello world runs, as do
  `throw`/`catch` and a caught NullReferenceException.
- The cscc-built basic `mcs.exe` reports "Mono C# compiler version
  2.6.4.0" and compiles hello world in about 1.5 s.
- Mono's own build then continued past basic and net_1_1_bootstrap into the
  net_1_1 profile.

**Follow-ups during the build.**

1. **`keyboards.resources` (Managed.Windows.Forms).**
   - Upstream's rule is `resgen keyboards.resx || cp
     keyboards.resources.prebuilt`. The prebuilt is deleted here.
   - **Real root cause:** the tarball's `keyboards.resx` is *malformed*.
     Python's XML parser and Mono's ResX reader both stop at line 30, column
     ~114812, where `...</root>` is followed by `4gAAAAs=</value></data></root>`.
     The upstream generator (`resources/create-keyboards.cs`, driven by
     `create-keyboards.sh`) overwrote an older, longer file without
     truncating it. So upstream resgen *always* failed and every Mono build
     silently used the prebuilt blob.
   - The prebuilt is also **stale**: deserialised, `keyboard_table` equals
     the source, but the third scan table has 49 entries where
     `create-keyboards.cs` defines 50 (checked with a small C# program on
     the new mono).
   - **Fix: regenerate from source**, as upstream intended (Makefile knob
     `GENERATE_RESOURCES=true` makes KeyboardLayout public). In
     `06-mono-2.6.4-cscc.sh`:
     - build once with an empty placeholder (pnet's C `resgen`);
     - rebuild net_1_1 SWF with `GENERATE_RESOURCES=true`;
     - compile and run `create-keyboards.cs` into a fresh `keyboards.resx`;
     - `resgen` it into `keyboards.resources`;
     - delete the SWF assemblies and run `make` again.

     The resulting resource references
     `System.Windows.Forms.KeyboardLayout[], System.Windows.Forms,
     Version=1.0.5000.0, PublicKeyToken=b77a5c561934e089`, exactly like the
     prebuilt.
   - An earlier attempt that compiled `KeyboardLayouts.cs` into the
     generator produced type references to a `create-keyboards` assembly.
     That was wrong, so it was discarded.
2. **Argument conversion.** MSYS2 argument conversion mangled
   `Apigen.exe /n:… /apiName:…` (RabbitMQ.Client). The prefixes are now in
   `MSYS2_ARG_CONV_EXCL`.
3. **`mdoc assemble` hung.**
   - It sat at 0 % CPU for over 13 min. Attaching i686 gdb showed the main
     thread inside `MessageBoxA` from bdwgc's `GC_default_on_abort("Too many
     heap sections")`: the GC abort is a modal dialog, so the build hangs
     instead of failing.
   - The private static bdwgc was built without `--enable-large-config`
     (MSYS2's own `mingw-w64-gc` uses it).
   - Fix: rebuilt the static GC with `--enable-large-config` (about 1 min),
     forced a relink of libmono (`libmono.la` removed), killed the stuck
     process by Windows PID with `taskkill`, and continued.

**End state (2026-10-08).** `bootstrap/06-mono-2.6.4-cscc.sh` on MINGW32
finishes `make` and `make install`.
- `~/dotnetboot/stage-m264-MINGW32` is 150 MB with the profiles 1.0, 2.0,
  3.5, compat-*, gac, xbuild and monodoc.
- The installed `mono` 2.6.4 runs `mcs` output (hello world).
- `gmcs` compiles and runs a **C# 3 program with LINQ, lambdas, extension
  methods and collection initializers** against `System.Core`:
  "C# 3/LINQ ok: 20 2 1".
- So **pnet (C) → mono-2.6.4 works directly on MSYS2 MINGW32 from source**,
  replacing Guix' mono-1.2.6 → 1.9.1 → 2.4.2.3 → 2.6.4.
- No `*.prebuilt`, monolite, `*.dll` or `*.exe` from the tarball was used.
  `prep-mono.sh` deletes them, and the only fallback (keyboards) is now
  regenerated from source.
- **Timings on win11 (4 cores, serial make):**
  - cscc compiles the basic mcs.exe in about 1.9 s and the basic mscorlib
    in about 1.2 s;
  - the last incremental pass, which rebuilt Managed.Windows.Forms in all
    profiles plus everything downstream and installed, took 7 min;
  - a from-scratch run of the script is estimated at well under 1 h,
    based on the per-stage logs (not measured in one go: no redundant
    rebuild).

## Generic MSYS2 lessons (apply to every later stage)

1. GCC 16 defaults to C23: use `-std=gnu17` and the Guix `-Wno-error=…` set.
2. Old `config.guess` cannot identify MSYS2: always pass `--build/--host`.
3. `-mno-cygwin` in configure scripts (treecc, mono ≤ 3.0): patch it out.
4. **LLP64:** `long` is 32-bit. Every `long`-as-pointer is a bug (pnet's
   `_findfirst` handle, mono's `amd64_is_imm32`, the switch magic offset).
   Grep for `long`/`glong`/`gulong` casts in any 2007–2012 code meant for
   64-bit.
5. The MSYS2 argument conversion mangles `/option` arguments to native
   executables: use `MSYS2_ARG_CONV_EXCL`.
6. `$(LN_S)` copies and is cwd-relative: use the `tools/ln-s-rel` wrapper.
7. DEP is always on for 64-bit processes (and on for 32-bit mingw executables
   too, since binutils defaults to `--nxcompat`): JIT memory must come from
   `VirtualAlloc(PAGE_EXECUTE_READWRITE)`.
8. Files that test `PLATFORM_WIN32` without including `config.h` silently
   take the POSIX branch (seen twice in mono 1.2.6).
