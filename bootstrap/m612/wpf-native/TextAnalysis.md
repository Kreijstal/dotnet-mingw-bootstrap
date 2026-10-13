TextAnalysis.cpp implements the four original WPF exports
CreateTextAnalysisSink, CreateTextAnalysisSource, GetScriptAnalysisList and
GetNumberSubstitutionList using actual Windows DirectWrite analysis interfaces.
All four baseline sink callbacks retain their results; no callback returns a
placeholder success or E_NOTIMPL.

The source borrows the caller's pinned UTF16 text, copies the locale, reports
paragraph direction, and owns the actual factory-created number substitution.
Returned substitutions follow COM AddRef conventions. The sink owns its own
references, keeps sorted half-open ranges, and overlays callback updates with
prefix/suffix preservation. Transactional replacement avoids leaking COM
references if allocation fails. Lists are borrowed until the next callback or
sink release. There is no global shared analysis state.

Native list nodes match original TextItemizer.h: Value first, then two UINT32
range endpoints and Next. Both specialized nodes are 24 bytes on x64; Range is
at offset eight and Next at sixteen. DWRITE_SCRIPT_ANALYSIS has a UINT16 script
and a four-byte native shapes enum, including its SDK-defined padding.

`wpf.sh check-text-analysis-native` uses the real system analyzer for script,
number, bidi and line-break callbacks. An independent forwarding recorder
checks resulting lists, mixed-script coverage and retained substitutions.
Additional checks cover borrowed text, copied locale, source direction, COM
identity, empty lists, overlap splitting, AddRef/Release, overflow and invalid
inputs. A counted substitution in the test measures ownership only; production
uses actual DirectWrite objects.

`wpf.sh check-text-analysis` invokes the complete original C++/CLI
TextAnalyzer.Itemize through the actual PresentationCore native delegates and
ClassificationUtility. It checks mixed-text spans and retained ItemProps after
native source/sink disposal and forced GC. This does not claim complete line
layout: LoGetEscString and the rest of Line Services still need implementation.
