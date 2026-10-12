#!/usr/bin/env python3
"""Stage WPF's unchanged primitive initialization and font-version parse block."""
from pathlib import Path
import re, sys
source, output = map(Path, sys.argv[1:])
text = (source / "src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper/Font.cpp").read_text()
initial = re.search(r"_version\s*=\s*System::Double::MinValue;", text)
if initial is None: raise SystemExit("WPF initial font version changed")
begin = text.index("                if(versionString->Length > 1)")
open_brace = text.index("{", begin); depth = 1; end = open_brace + 1
while depth:
    if text[end] == "{": depth += 1
    elif text[end] == "}": depth -= 1
    end += 1
block = text[begin:end]
output.write_text("""// The probe supplies a version string; initialization and parse block are unchanged WPF source.
using namespace System;
public ref class WpfFontVersion {
    double _version;
public:
    WpfFontVersion() { """ + initial.group() + """ }
    double Initial() { return _version; }
    static double Parse(String^ versionString) {
        double version = 0.0;
""" + block + """
        return version;
    }
};
""")
