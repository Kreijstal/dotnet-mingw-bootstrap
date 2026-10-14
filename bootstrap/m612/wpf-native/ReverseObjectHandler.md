# Source native reverse object handler

LocbkGetObjectHandlerInfo now supplies the native reverse object (ID zero) through
the original WPF GetObjectHandlerInfo callback. Its producer and consumer are both
this source engine; original WPF forwards an opaque pointer without inspecting
the record. ReverseObjectHandler.h declares that private protocol explicitly,
without claiming Microsoft's unpublished native handler layout.

The returned context-bound function performs opening/closing embedding level
transitions with the original paragraph base and level-125 bounds. Body and list
marker controls obtain the original callback's handler on first use, validate its
version/kind/owner/function and retain the successful binding per context. Failed
or malformed callbacks never publish a binding. Coalesced controls, source CPs,
zero glyph advances, wrapped/cloned embedding and existing visual geometry stay
in the line model. Native registration is callable during formatting; no client
callback runs under the registry mutex. A borrowed handler rejects a destroyed
owner. Active line-formatting scopes retain the owner and prohibit its destruction,
so transitions in that scope avoid a registry lock for every control. Other
borrowed-handler calls still validate the live registry; scope teardown restores
the previous thread-local owner before releasing the formatting guard.
Unsupported object IDs do not overwrite an unknown protocol's buffer.

Native tests check real transition results and bounds for LTR/RTL bases, null/
invalid/stale contexts, callback failure and malformed/foreign/incomplete retry,
stable binding reuse, nested/continued source and geometry and clean invalid
closing scopes. The original managed formatter-context fixture independently
marshals the private record/function, calls its real transitions across GC and
checks owner teardown. Normal SDK consumers run that fixture and the original
bidi/list-marker drawing, source, caret/point and continuation checks in shared
and portable execution. General bidi/optimizer conformance and other documented
native text/paragraph gaps remain unfinished.
