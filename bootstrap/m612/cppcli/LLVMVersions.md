# Checksum-locked Clang plugin versions

The frontend supports LLVM/Clang 22.1.8 and 23.1.3 as separate, exact ABI targets.
cppcli.sh reads llvm-config's version, selects the corresponding checksum manifest
and CLI parser patch, and passes the version to CMake's exact LLVM lookup.
LLVM22 retains its existing 18 parser files; LLVM23 uses 19, including the upstream
ParseReflect.cpp translation unit. This does not add managed reflection syntax
support to the compiler subset.

The LLVM23 patch is a three-way port of the existing CLI changes against the
original LLVM22 and LLVM23 release-tag sources. Its full-context hunks preserve
upstream parser changes instead of inserting CLI hooks at old line numbers.
Every input parser file is checksum-verified before patching. Offline preparation
and switching between the two versions remove stale parser translation units.

CMake records clang-version.txt beside the plugin and driver, and writes the
selected version into generated capabilities.json. The driver verifies an exact
Clang version before loading the plugin. A missing record retains the legacy
LLVM22 behavior for old build trees; new packages install the generated record.
The guard also rejects version-prefix collisions such as 23.1.30.

The LLVM23 compiler builds with the trusted MSYS2 toolchain and source-built SDK.
Its existing 18 package check groups exercise runtime behavior, metadata, arrays,
tracking, pinning, interop and rejection cases. Original WPF DirectWriteForwarder
and System.Printing compile with this compiler; their assembly/JIT checks and
printing properties, native spooler enumeration and local XPS readback pass.

Windows console code page is a test precondition for redirected Unicode output.
The test session selects UTF-8 (65001) and restores the previous page on exit.
Both the new and saved LLVM22 control applications produced replacement characters
under code page 850; their UTF-16 IL literals were correct. No source assertion is
removed or weakened to accommodate that host setting.

This changes toolchain support, not the advertised language subset or mixed-mode
PE capabilities. Every other LLVM version requires its own reviewed source port
and matching plugin build; a version check must not be bypassed to load an ABI-
incompatible plugin.
