The classification source port uses ICU's Unicode properties through the
explicit `WpfGetClassificationTables` ABI. `ClassificationTableView` contains
two pointers and an integer count (24 bytes on x64). Every plane/block has a
real pointer; each Unicode value maps to a packed eight-byte CharacterAttribute.
The tables and deduplicated attributes stay alive for the loaded native module.
WPF's original ScriptID, ItemClass, DirectionClass and flag numbers are generated
from UnicodeClasses.cs rather than independently assigned.

This supplies the properties the original managed Classification methods read.
Unused legacy mirroring/combination pointers and their unknown native layout
were removed from that managed source. No MILGetClassificationTables export is
fabricated. Native table initialization uses the installed trusted MSYS2 ICU
library, checks class-index capacity, and returns HRESULT failures instead of
letting C++ exceptions cross the ABI.

This is a source adaptation, not a copy of Microsoft's unavailable 2007 tables.
It uses the installed ICU Unicode version. Fast-text eligibility is conservative:
combining marks, controls, RTL, unsupported scripts and contextual scripts route
to full formatting. Known script IDs retain WPF's legacy numbering; newer
scripts use Default and stay outside the fast path. Ideographs and precomposed
Hangul/Kana use nominal-glyph eligibility where applicable. LineBreak stores
ICU's property value, which current managed Classification callers do not read;
a future native line-service adapter must map it to its own contract.

The original managed bidi engine has no isolate direction classes. These
formatting controls map to BoundaryNeutral and remain complex. Full modern
isolate semantics require a separate bidi-engine port. Complete line-service,
paragraph and natural-language formatting remains unfinished.

`wpf.sh check-classification` verifies all 1,114,112 Unicode values against ICU
for table bounds, bidi, line-break, digit/letter/space/ideographic/extended fields,
and script/item ranges. It checks concurrent pointer lifetime. The C# peer walks
all scalar entries and all UTF16 entries, verifies ABI size/offsets, representative
script/flag values, combining/joiner/variation behavior, and forced GC.
