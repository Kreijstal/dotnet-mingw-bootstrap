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
Queries join graphemes across client-run boundaries, retaining each requested
code point's source run metadata. Glyph maps must start at zero, remain monotonic and name actual glyphs, as
required by the original GlyphRun and expansion/placement consumers. Malformed
maps fail before glyph placement; other advanced geometry still needs work.

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

Inline objects (original object ID 1) obtain source InlineInit format/draw
callbacks through GetObjectHandlerInfo. Source-generated ObjDim/InlineInit
records preserve the original x64 ABI. Format receives the actual preceding pen
position and original infinite margin; valid object dimensions contribute line
metrics and a retained atomic width. Draw receives its original identity, flow,
width and baseline origin. Queries retain object ID/geometry with zero glyphs;
glyph enumeration omits objects and preserves source positions around them.
Wrapping applies the original LineBreakCondition combination table at object
edges, including all 16 adjacent-object cases. Public tests cover fixed/flexible
sizes, a three-character source span, remaining width, caret/selection, wrapping,
callback exceptions and original TextBlock InlineUIContainer rendering/hit tests
across GC. Position-dependent objects following aligned tabs remeasure at the
current pen until retained field widths and positions agree. Full fields and wrapped
prefixes settle independently; callback failures and nonconvergence remain explicit
errors, while ordinary missing-tab-fit errors can use the wrapping fallback.

Regular inter-word justification requests original nominal/glyph expansion
callbacks using source-generated 28-byte neighbor, 48-byte character and 80-byte
glyph records. Three callback capacity levels distribute remaining visible
width across eligible whitespace; negative capacities and callback failures
propagate. Original glyph advances and nominal minimum widths remain retained,
while actual advances, enumeration, caret/selection cells and drawing use the
expanded geometry. Trailing spaces do not receive expansion. Paragraph-final,
explicit-break and no-opportunity lines stay ragged. Public Latin/Indic tests
cover Ideal/Display modes and actual justified TextBlock rendering/hit testing.
Aligned-tab justification, other justification modes, ink insertion and full
compression/optimal algorithms still explicitly reject or require further work.

Min/max paragraph mode retains the entire paragraph without column wrapping or
justification. Shared Unicode/cluster/object-edge break geometry determines the
widest indivisible segment; hard lines are measured separately for maximum
width. Trailing spaces remain in total widths but are excluded from visible
widths. Tabs use actual stops from each measured segment's paragraph origin,
and inline objects preserve atomic dimensions and source break conditions.
Native exact and public nominal/Indic/NBSP/hard-break/trailing/empty/cache tests
pass in Ideal/Display modes, including original FormattedText.MinWidth and
intrinsic/wrapped TextBlock layout. Streaming/range measurement optimization,
full hyphenation/compression and RTL width coverage still require further work.

Optimal formatting and bidi reversal objects remain unsupported and return
explicit errors.
Complete hit testing, RTL/nonmonotonic shaping clusters, full bidi and paragraph
layout require further work. This does not establish full Line Services
compatibility or complete WPF packaging.


LoAcquirePenaltyModule creates an owned wrapper around retained source LS
context state. LoGetPenaltyModuleInternalHandle exposes that state's opaque
identity, and LoDisposePenaltyModule releases it with invalid/double-release
checks. Native lifetime tests and original TextPenaltyModule through PtsContext
pass in both formatting modes across GC and disposal. These exports implement
resource ownership; they do not claim penalty scoring or optimal paragraph
formatting. The paragraph-service context implementation separately retains
original generated callback/configuration records and installed method tables.

Cross-run query cells use Unicode grapheme boundaries over the retained source.
Internal reversal controls are excluded from segmentation but remain in LS source
ranges. Joined cells cover one contiguous visual interval, retain glyph counts and
actual run identities, and expose stable caret sublines across internal reversal
scopes. Unknown noncontiguous geometry still fails explicitly. PresentationCore's
FullTextLine recognizes cells spanning source runs and maps their LSCP endpoints
back to external source CPs for caret lengths. This prevents hidden controls from
becoming caret stops while keeping ordinary within-run character navigation.

Native fixtures cover split combining marks, surrogate pairs, ZWJ emoji and flags,
exact complete-cell geometry, distinct per-CP client run metadata, both point edges,
tiny-width wrapping and disposed-line rejection. The public SDK consumer covers
64 split-grapheme combinations per execution: Ideal/Display, LTR/RTL paragraphs,
style changes, tiny/full widths, Latin combining/Indic/emoji/flags. Forward/backward
carets, every interior code-unit query, hit testing, selection width, glyph source
coverage, compacting GC and real raster output are checked, alongside ordinary
same-run character carets. Shared and portable executions remain separate checks.

FullMixed justification (original LsKJust value 4) accepts provider expansion
capacity at nonfinal Unicode grapheme and complete glyph-cluster boundaries,
including non-space boundaries. InterWord (value 0) retains whitespace eligibility.
Shared source grapheme segmentation excludes internal reversal transport controls
and prevents expansion inside a user-perceived character. The final visible
non-space cluster receives no gap after it. Callback priority/capacity bounds,
nominal advances and original glyph expansion types remain authoritative.

Native assertions compare exact nominal/shaped mixed and interword geometry and
reject malformed first/range/decreasing cluster maps before placement callbacks.
The mixed SDK consumer checks optimal no-space Latin and Indic paragraphs in both
formatting modes/directions, single/multiple columns, exact source/page completion,
caret/point queries, real raster/GC, plus direct mixed/interword caret/selection
comparisons. Full ink expansion, compression and other advanced algorithms remain
unfinished.

Aligned flexible objects retain their last measured pen. Tab layout iterates the
original object callbacks until exact native positions and dimensions agree,
within64 passes. Initial prefix collection does not prematurely settle incomplete
fields. Final line metrics include the accepted object ascent/descent and their
combined height. Fixed-size callbacks keep their original single-size caching.
The managed TextStore cache keys flexible metrics by source CP, pen and margin;
a failed measurement cannot publish a new cache key.

Native fixtures check exact Left/Center/Right/Character geometry, integer fixed
points, final heights/drawing, populated callback failure, bounded oscillation
rejection and context reuse. Public fixtures check both formatting modes, eight
complete-field and eight wrapping cases, source ownership, remaining width,
caret/selection, real raster and GC; initial/reformat/draw exceptions and reuse are
also checked. Display endpoints follow the original independent pixel rounding;
assertions use those exact rules rather than a larger arbitrary tolerance.

Justification retains each visible tab's actual selected alignment, including
incremental fallback. Only the field following the last visible tab expands.
Earlier aligned fields keep their nominal anchors, and unused aligned stops do
not disable a final left field. Final center/right/character fields still reject
justification pending their anchor-preserving expansion policy. Native checks
cover nominal/shaped and InterWord/FullMixed modes; the public consumer checks
Ideal/Display, split runs, earlier four-way anchors and incremental fallback,
exact width/caret/point/selection, source completion, raster and moving GC.
