# Original WPF footnote query callbacks

The three subtrack/subpage count/info APIs validate an empty collection against
retained layout and the original callbacks. They require a live handle of the
right kind and owner context; they query only retained text CP ranges and actual
nested object content. Containers/subpages reenter their original native queries,
floater content uses its original count callback, figures visit retained native
subpages and tables call each shared cell once. Callback errors and invalid
negative counts propagate; callback cycles fail before recursion can overflow.

Upstream TextParagraph.GetNumberFootnotes returns zero, and PtsHost.GetFootnotes
and the footnote creation callbacks are unimplemented. Accordingly a positive
count returns NotImplemented instead of being exposed as supported footnote
placement. Missing count callbacks also fail explicitly. These APIs close the
actual source WPF wrapper paths for its supported empty collections, not general
footnote layout.

Empty subpage info preserves indexStart as indexLim, reports the real LTR/RTL
writing direction and never modifies caller array entries. Capacity, index and
required output pointers are checked; failed queries reset scalar outputs. The
FSFTNINFO ABI comes directly from the original Pts.cs generator and is verified
against managed field sizes and offsets.

Native regressions exercise retained/cloned CP ranges, recursive containers,
floating/figure content, shared table cells, original callback errors, negative/
positive counts, cyclic reentry, stale/foreign/kind/null queries, indexed array
sentinels, LTR/RTL direction and exact resource cleanup. Existing SDK subpage and
subtrack consumers also call the original PtsHost wrappers on finite chunks,
bottomless content, updates and synchronization. Rendering/source/GC checks and
shared/portable execution remain required.
