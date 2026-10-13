This source port comes from `~/git/msys2-ur/mingw-w64-wpf-penimc`, commit
`8744242` (in tree `17b4cfa4c478775f0769b76e7a40a2ec60af0887`).
The bootstrap defaults version metadata to 8.0.0 and avoids rewriting unchanged
generated version headers. It compiles the original WPF v8.0.0 PenImc sources,
generates COM headers/proxies/type libraries with MSYS2 widl, and uses the trusted
ReactOS ATL headers. No Microsoft prebuilt DLL, ATL library or MIDL is used.

`wpf-penimc.sh build check` validates all exports, the merged COM proxy metadata,
class-factory lookup and unload behavior. It does not prove input on a physical
pen/tablet device.
