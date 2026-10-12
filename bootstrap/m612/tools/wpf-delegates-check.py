#!/usr/bin/env python3
"""Retain WPF's four text-analysis delegate declarations in a runtime probe."""
from pathlib import Path
import re
import sys
source, output = map(Path, sys.argv[1:])
text = (source / "src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper/TextAnalyzer.h").read_text(encoding="utf-8")
declarations = re.findall(r"\bprivate\s+delegate\s+[^;]+;", text)
names = ("CreateTextAnalysisSource", "CreateTextAnalysisSink", "GetScriptAnalysisList", "GetNumberSubstitutionList")
if len(declarations) != 4 or any(sum(re.search(r"\b" + name + r"\s*\(", d) is not None for d in declarations) != 1 for name in names):
    raise SystemExit("expected four upstream text-analysis delegates")
native = source / "src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper/CharAttribute.h"
output.write_text("#include <windows.h>\n#include \"" + native.as_posix() + "\"\n" + """
using namespace System;
namespace MS { namespace Internal { namespace Text { namespace TextInterface {
""" + "\n".join(declarations) + """
}}}}
using namespace MS::Internal::Text::TextInterface;
public ref class WpfDelegatesMarker abstract sealed {
public:
    static int Mask() { return CharAttribute::NeedsCaretInfo | CharAttribute::IsStrong; }
    static void* Sink(Delegate^ value) { return safe_cast<CreateTextAnalysisSink^>(value)(); }
    static void* Script(Delegate^ value, void* input) { return safe_cast<GetScriptAnalysisList^>(value)(input); }
    static void* Number(Delegate^ value, void* input) { return safe_cast<GetNumberSubstitutionList^>(value)(input); }
    static void* Source(Delegate^ value, Char* text, unsigned int length) {
        void* output = nullptr;
        int result = safe_cast<CreateTextAnalysisSource^>(value)(text, length, nullptr, nullptr, true, nullptr, false, 7, &output);
        if (result != 42) throw gcnew InvalidOperationException();
        return output;
    }
};
""", encoding="utf-8")
print("four upstream TextAnalyzer delegate declarations retained unchanged")
