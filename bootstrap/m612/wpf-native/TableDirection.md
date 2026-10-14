# Independent table flow direction

The source paragraph formatter preserves the table's horizontal flow direction
when it differs from its containing track. Original TableParagraph.GetTableProperties
reports fswdirTable independently of the track direction. Table placement,
AutofitTable and margin collapse still use parent track coordinates; row properties,
distribution, ordinary and spanning cell formatting, and SetCellHeight receive the
table direction. FsQueryTableObjDetails returns that same table direction without
mirroring its placement rectangle into cell coordinates.

The native fixture checks all four parent/table direction combinations for ordinary
and spanning rows, actual callback direction arguments, query geometry and exact
resource disposal. Existing callback failure and continuation checks remain active.
The original managed ABI remains 482 fields.

The RTL SDK consumer checks all four page/table direction combinations in Ideal and
Display mode, using real red/blue cell backgrounds to verify logical column order.
The complete public table consumer also runs with each opposing page/table direction:
ordinary and explicit columns, margins/padding, column spans, nested tables with the
opposite direction, row and spanning-cell continuations, page/source mapping,
regeneration after page disposal, tiny forced-progress pages, empty content, and
bottomless edits/scrolling. The spanning-background check measures the same minimum
continuous red height across the raster so both physical column orders are covered.
All consumers run as shared-framework and portable self-contained applications.

This supports horizontal LTR/RTL tables. Vertical writing directions, floating tables,
non-default native table alignment, header/footer rows and other documented advanced
table features remain unsupported. It does not establish full native layout parity.
