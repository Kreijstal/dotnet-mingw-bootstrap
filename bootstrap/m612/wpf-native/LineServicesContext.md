This implements Line Services context setup and owned configuration, using
record layouts generated from original WPF LineServices.cs. LoCreateLine now
fetches actual client text and metrics, measures nominal advances or invokes
the real shaping/glyph-position callbacks, and retains the result. LoDisplayLine
and LoEnumLine use retained arrays through the original drawing/enumeration
callbacks; LoDisposeLine releases its owned state.

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
window now renders Latin and shaped Indic text with glyph-pixel checks, and
public TextFormatter returns retained indexed glyph runs after forced GC.

Line creation supports nominal/shaped runs, paragraph/line separators and
Unicode wrapping. It tracks trailing spaces across runs and returns measured ascent,
descent, height, widths and text positions. It propagates callback failures and
keeps outputs empty on failure; context reentry while constructing a line is
rejected. Callbacks run outside the registry lock. Shaping arrays follow the
actual eight-byte GlyphOffset and original delegate ABI.

ICU line opportunities are checked against measured prefix widths and complete
shaping clusters. Emergency wrapping uses grapheme boundaries, including those
that span client runs; overflow wrapping retains an oversized whole word.
Trailing ordinary spaces do not consume the visible column allowance. A cut
shaped run is reshaped by the original callbacks before retention. The engine
currently fetches the remaining paragraph through its separator before choosing
a break so a client run boundary never becomes an artificial word boundary.
This can repeat work on long paragraphs and needs a streaming optimization.

LoAcquireBreakRecord, LoCloneBreakRecord and LoDisposeBreakRecord own native
continuation state with the exact next position, paragraph start and context.
Clones survive disposal of the line and original record; wrong positions and
foreign contexts fail. WPF's ordinary single-line public GetTextLineBreak path
passes a null native line pointer, so the native test separately exercises these
exports rather than relying on public formatting to prove record ownership.

LoQueryLineCpPpoint and LoQueryLinePointPcp return real retained character/
grapheme/shaping-cluster cells, their measured advances and original client run
identities. The one supported subline contains the original run and line
geometry. Point queries clamp to the first/last positive-width cell; paragraph
separators retain their own zero-width position and original run identity.
The original nested query layouts are generated from LineServices.cs:
LsQSubInfo 120 bytes and LsTextCell 40 bytes on x64. Public Indic leading/trailing
hits, caret navigation, distances and selection rectangles pass after GC.
Queries reject RTL/nonmonotonic cluster maps and graphemes spanning client runs;
cross-run caret ownership remains unfinished even though wrapping respects
those graphemes.

LoDisplayLine obtains underline and strikethrough metrics through the original
callbacks and draws each retained decorated run with its original identity,
width, flow, drawing mode and clip rectangle. Single/double-line offsets and
thicknesses follow the source-generated LsULInfo/LsStInfo records. Callback
failures and invalid metrics propagate. Original managed drawing implements
custom pens and underline/strikethrough/overline/baseline locations; public
TextFormatter raster checks cover all four locations across compacting GC.

Tabs are retained as individual layout runs. Explicit left/center/right/decimal/
character stops align the following field across client runs; incremental stops
continue after the last explicit stop. Tab caret/selection geometry retains the
actual gap. Original EnumTab preserves source CP, leader and geometry, while
leaders are measured and drawn using original text callbacks. The public test
covers WPF's four exposed alignments, one-run/split fields, dotted leaders,
incremental stops, aligned-field wrapping and drawing/enumeration across GC.
Each legal Unicode break candidate aligns only its retained field. The selected
prefix is remeasured/reshaped and its real visible width rechecked; if contextual
advances make it wider, an earlier legal candidate is tried. A leading
indivisible tab/word outside the column advances with unavoidable overflow under
the selected wrapping policy. Native/public checks cover fitting retained fields,
out-of-column tabs, cloned continuation records and complete source coverage.
Explicit-only stops with zero incremental distance are evaluated per retained
field even if the full field has no fallback stop. Candidates without usable
stops are skipped; invalid configurations still fail. Candidate evaluation
scans retained fields and needs optimization for long tabbed
paragraphs. Leaders currently repeat whole measured characters centered in the
gap; exact legacy leader spacing and non-LTR tab behavior require validation.

Optimal/min-max formatting, inline objects, bidi reversal objects and
justification remain unsupported and return explicit errors.
Complete hit testing, RTL/nonmonotonic shaping clusters, full bidi and paragraph
layout require further work. This does not establish full Line Services
compatibility or complete WPF packaging.
