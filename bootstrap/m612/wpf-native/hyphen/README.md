# Source hyphenation inputs

The native WPF replacement embeds the 36 UTF-8 text dictionaries in `patterns/`
and builds the C engine in `engine/` from source. `manifest.json` records the
upstream repositories, exact commits and SHA-256 of every copied input. The
header generator verifies all inputs before emitting the embedded table. The
original dictionary files are unchanged; no binary dictionaries or Microsoft
natural-language components are used.

The engine is Hunspell/Hyphen, pinned to commit
482d218738a5affdea467883eb01d5518312ae80. Its files carry the original
GPL-2.0-or-later/LGPL-2.1-or-later/MPL-1.1 choice of licenses; this integration
uses the MPL-1.1 terms. All original notices and license texts remain in `engine/`.

The dictionaries and notices come from the existing Mypal68 source checkout,
pinned to commit 0190d4f3c10a29dfd1f498a8fbf21252cf91356e. Each locale directory
retains its original LICENSE or English README/license notice. Their original
terms vary (including MPL-2.0, TeX/LPPL and permissive licenses); the package
installs the per-file notices rather than describing them as one license.

The original WPF NlHyphenate ABI exposes a bitmask of positions and cannot carry
spelling replacements. Only standard opportunities that preserve all original
characters are returned. UTF-16 positions, Unicode case normalization and
original grapheme boundaries are retained. Unsupported locales return an empty
mask. The loader maps regional English to the bundled US patterns, German to
reformed orthography (Swiss to de-CH), and Serbian to the bundled sh patterns.
These dictionary choices are explicit and do not claim Microsoft-identical
linguistic results.
