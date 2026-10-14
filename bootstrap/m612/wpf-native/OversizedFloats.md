# Progress for floating groups larger than a page

An occupied page still defers an intact attached group. On an empty page, if one
object has been accepted but the next object's requested placement cannot fit,
retain the accepted object and stop the host line before the next anchor. The
native text continuation carries that exact CP and its original line-break record
into the next page. The unconsumed object is fetched again from the original
managed callbacks; speculative content and clients are released.

Formatting first obtains a naturally fitting line range and only shortens it at
the deferred anchor through the original ReconstructLineVariant callback. Leading
text therefore keeps its width limit. Newly discovered inline anchors can trigger
a retry of their provisional row. Accepted rows/objects keep their source owner;
later anchors do not paint or acquire a source range on the current page.

The retained prefix is marked fForceBroken in the original line description.
Managed TextParaClient reconstruction uses its recorded CP range for these lines,
as it already does for optimal lines. Two line-result helpers only receive CP
bounds, so they reconstruct that exact retained range directly. Rendering,
geometry, selection and point queries therefore agree with the saved prefix.
No native record layout or exported API is added.

Native tests verify original callback invocation, first/second anchor ownership,
continuation after the first page is destroyed, exact remaining CPs and balanced
accepted/speculative line, break-record, object and client destruction. Public
SDK tests exercise both text modes/directions, two and three text floaters,
fixed-size UI content, short/long inline prefixes, occupied-page leading text and
optimal formatting. Every tested text character has one page owner, objects do
not repeat, and disposed pages/GC retain the original geometry. Both shared-
framework and portable execution remain required, along with existing floater and
optimal paragraph regressions.

This adds progress for an oversized group of individually fitting objects.
General polygon/object/table/subpage interactions and other documented layout
restrictions remain unfinished.
