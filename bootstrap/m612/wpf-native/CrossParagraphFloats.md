# Floats across original WPF paragraph containers

Ordinary WPF paragraphs are represented by container paragraphs with text leaves.
Earlier objects therefore need to propagate through the container callback path,
in addition to passing directly from one text leaf to the next.

Text and track state distinguish text advance from the full extent of owned
floating content. A completed paragraph can advance the next text/container by
its text height; a final paragraph or transition to a non-text block retains the
outstanding object extent. Object clients, source ranges and drawing remain owned
by their anchor paragraphs. Later paragraphs receive rectangle/flow snapshots,
not another object client or drawing owner. Snapshots expire below the current
position and reset at page/column boundaries. Container callback scopes pass
geometry into nested tracks and isolate the local tracks inside floating content.

Inherited obstacles participate in line-area intersection, object collision,
anchor postponement and selective floater clearing. Geometry and wrap/alignment
sides transform when a paragraph changes direction. Bottomless replacement,
comparison and synchronization retain the snapshot fields. Context/record ABI
does not change.

Opposite-direction paragraphs exposed a line-end bidi error: trailing whitespace
retained the embedded run's level and shifted actual glyphs/carets into the
excluded area. LineServices now performs the UAX #9 L1 line-end level reset after
wrapping. It splits a directional run at the whitespace boundary and measures
the retained parts through the original callbacks, preserving source CPs and
continuation levels. Drawing, enumeration and caret queries share that geometry.

The public SDK consumer checks 24 finite wrapping cases (both text modes,
document directions, left/center/right objects and matching/opposite following
directions), including optimal centered paragraphs and 80 caret/point queries per
case. It checks 16 cross-paragraph clearing cases, four bottomless resize/scroll
cases and eight short/multipage two-column cases. All source characters retain
one page owner, objects do not repeat, and disposed pages/GC retain geometry.
Both shared-framework and portable apphost executions use the SDK's actual
native library. Existing bidi, native line/paragraph and selective clearing
regressions remain required.

General polygons, forced oversized object groups, advanced float/table/subpage
interactions and other documented layout restrictions remain unfinished.
