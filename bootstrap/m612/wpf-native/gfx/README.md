This directory carries the MinGW graphics port from
`~/git/msys2-ur/mingw-w64-wpf-gfx`, commit
`8744242` (present in tree `17b4cfa4c478775f0769b76e7a40a2ec60af0887`).
Its six source patches apply to WPF v8.0.0. The bootstrap copy defaults the
version resource to 8.0.0, removes the later WpfGfxSwitches.cpp build input, and
reads the handshake constant from the actual source header in its smoke test.
Patch 0007 uses the existing static null-safe matrix overload in the renderer's
clipping path. Clang otherwise assumes a member call's `this` is non-null and
removes the intended identity-transform fallback, causing a native access
violation when a themed window draws through the software rasterizer.

`wpf.sh build-gfx check-gfx` builds a private patched copy of WpfGfx/Shared,
regenerates text shaders and compiles HLSL with trusted MSYS2 vkd3d, then checks
shader bytecode, every upstream export, and the protocol handshake. It does not
consume a prebuilt Microsoft native DLL or shader compiler.

The inherited port disables WPP tracing and does not translate SEH exceptions
into C++ exceptions through `_set_se_translator`. These limitations remain;
export/handshake checks do not prove all rendering, multimedia or failure paths.
