# WPF legacy spelling bridge

`NlLoad`, `NlGetClassObject` and `NlUnload` implement the original
`NLGSpellerInterop.cs` factory and lifetime ABI. The native objects own their
resources through COM reference counts; releasing the loader does not invalidate
surviving contexts, chunks, enumerators, sentences or segments. COM apartment
initialization remains the caller's responsibility.

`wpf-nlg-header.py` derives interface GUIDs, class GUIDs, range-role values and
method order from the original managed declarations. The six interfaces have
20/60/28/18/32/8 methods after IUnknown. Original unused `stub_*` entries return
E_NOTIMPL. Implemented methods preserve HRESULT/out values, VARIANT arguments,
VARIANT_BOOL values and the original suggestions enumerator's
VT_UI2|VT_BYREF pointers. Sentence enumeration returns owned VT_UNKNOWN values;
clones retain their own positions and snapshots.

ICU provides sentence/word segmentation in the original UTF-16 coordinate space,
including offsets after surrogate pairs. Chunks copy their input, and enumerators
retain independent snapshots. Spelling and suggestions use Windows'
ISpellCheckerFactory/ISpellChecker services, the same OS dependency used by
upstream WPF's WinRT provider. No prebuilt spelling engine is bundled. Spelling
therefore requires Windows 8 or later and an installed provider for the requested
culture; unavailable cultures fail explicitly. Word-breaking-only mode works
without a spelling dictionary.

Verification mode reports errors without suggestions. Word-breaking mode does not
check spelling. Custom lexicons decode UTF-8 and BOM-tagged UTF-16 LE/BE, match
words without case and honor #LID culture metadata. Contexts can add/remove
lexicons and release all lexicons without changing the user's persistent OS
dictionary. Compound subsegments and legacy German/French pre/post reform
selection remain explicitly unsupported.

IsSpellSuggestingMWEs enables whole-input ComprehensiveCheck using the original
Windows provider. Native UTF-16 error spans become incorrect segments, retaining
unaffected ICU word fragments and sentence ordering. Replacement actions return
the provider's exact replacement; deletion actions return an owned empty-string
suggestion. Suggestion-list actions use the provider's Suggest enumeration.
Verification mode still has no suggestions, and word-breaking-only mode does not
invoke spelling even when multiword is enabled. Disabling the option restores the
original independent-word checking path.

Explicit input spans separated by embedded NULs are checked independently while
preserving offsets into the original buffer. Provider ranges/actions are validated
before publication, and overlapping errors are rejected. Culture-scoped custom
lexicons suppress spelling errors without suppressing contextual deletion. Snapshots
retain their original source, roles and suggestions after chunk/mode changes.

The companion managed patch retains a culture-aware custom-word set in each
WinRT provider, alongside the original Windows dictionary registration. Original
spelling segments consult that set before asking the OS checker. This fixes the
demonstrated native-only case where Windows accepts culture-scoped registration
but still reports the custom word as incorrect. Dictionary removal/clear updates
both sets; the original file and language scope are preserved.

`check-spelling-native` checks factory identity/error contracts, original COM
marshaling, unavailable cultures, explicit unsupported options, parent-independent
snapshots, enumeration cloning and reload. `check-spell-check-sdk.sh` uses the
original managed NLG wrapper and public TextBox spelling APIs. Its test-only
reflection selects NLG after the original Speller initializes, allowing both
providers to exercise detection, suggestions, correction, IgnoreAll and custom
dictionaries. UTF-8 and UTF-16 LE/BE lexicons are checked with and without culture
headers, along with removal and source-byte preservation. The consumer builds
offline, then runs with both the shared framework and a portable apphost.
