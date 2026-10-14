# Container clearing of inherited floaters

FsFormatSubtrackFinite, FsFormatSubtrackBottomless and FsUpdateBottomlessSubtrack
accept the original horizontal clear enumeration. Before formatting container
source, the formatter transforms inherited obstacles into the container direction
and advances past only the selected side. Floater alignment determines its logical
side; non-floater obstacles use their occupied horizontal geometry.

Accepted clearance contributes to consumed height and is reported as top space.
The exported subtrack content rectangle excludes that leading gap so original
managed content/background arrangement uses the cleared origin. Bottomless updates
publish the new gap with the replacement tree; invalid inputs and callback failures
retain the previously queryable tree. Comparison includes the gap, while shifting
preserves its height.

A finite gap that leaves no usable space returns an immutable source continuation
without consuming the container or publishing empty child content. If no child
source is accepted, the gap is not consumed. Keep-chain rollback retains clearance
only when accepted child paragraphs remain. Empty containers do not create a gap.

Native fixtures verify original inherited floating callbacks, selective child line
positions/widths/heights, exact no-progress deferral/resume and resource disposal.
All valid clear values are checked without obstacles; invalid values clear outputs
and preserve existing bottomless content. The original 482-field managed ABI check
remains active.

The normal SDK consumer covers 64 finite cases in Ideal/Display, LTR/RTL page and
container directions, each clear value and occupied-page deferral. It verifies
exact selected-side raster positions, unchanged earlier source, complete single
page ownership, regeneration, compacting GC and section background placement.
Four bottomless cases resize an inherited floater, change the section clear
property and scroll to the real source end. Shared and portable runs execute all
68 cases. Existing cross-paragraph floating regressions remain separate gates.

This is an incremental horizontal container port. Vertical layout, arbitrary
polygon/object geometry and the other documented advanced layout gaps remain
unfinished. Styled container margin/border interactions need broader conformance
coverage beyond the zero-margin/padding sections exercised here.
