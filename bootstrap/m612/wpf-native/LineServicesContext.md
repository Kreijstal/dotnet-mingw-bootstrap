This implements Line Services context setup and owned configuration, using
record layouts generated from original WPF LineServices.cs. It does not return
a fabricated formatted line. LoCreateLine and the later line/paragraph APIs
remain absent until their behavior is implemented.

LoCreateContext retains callback addresses, copies installed handler methods,
sets the client context cookie, and registers a real owned context. LoSetDoc,
LoSetBreaking and LoSetTabs validate and store device/strategy/tab state for the
future line engine. A mutex protects registry access and configuration; invalid
and destroyed handles are rejected without dereferencing them. Managed code
keeps the callback delegates alive through its original LineServicesCallbacks
owner. No callback is invoked while that registry lock is held.

LoGetEscString supplies the source engine's single-character control vocabulary:
paragraph separator U+2029, line separator U+2028, hidden word joiner U+2060,
NBSP U+00A0, object terminator U+FFFB and object replacement U+FFFC. The UTF16
strings stay alive for the module lifetime. These are explicit source-engine
choices, not a claim to reproduce unknown private legacy marker values. The
future line engine must interpret this same vocabulary.

WpfGetLineServicesContextState is an explicit diagnostic copy of retained state;
it does not expose registry-owned mutable pointers or claim that text was laid
out. The native and C# checks validate state, failure isolation, copying,
concurrent access and teardown. Their original C# marshal/native x64 layouts
agree: LsContextInfo 760 bytes, pols offset 120, pfnFetchPap offset 184,
fDontReleaseRuns offset 752, LscbkRedefined 24, LsTbd 12, diagnostic state 64.

The C# peer constructs original TextFormatterContext, whose original constructor
populates callbacks, creates the native context, sets its four 1440-twip device
resolutions, selects CJK breaking and keeps callback owners alive. Creation,
configuration, forced GC and original repeated managed teardown pass. The text
window now reaches missing LoCreateLine; that failure remains a required gate.
