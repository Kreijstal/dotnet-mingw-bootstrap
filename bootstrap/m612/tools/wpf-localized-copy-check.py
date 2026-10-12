#!/usr/bin/env python3
"""Compile the unchanged WPF CopyTo method in a managed collection context."""
from pathlib import Path
import re
import sys

source, output = map(Path, sys.argv[1:])
header = source / "src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper/LocalizedStrings.h"
text = header.read_text(encoding="utf-8")
matches = list(re.finditer(r"\bvirtual\s+void\s+CopyTo\s*\(", text))
if len(matches) != 1:
    raise SystemExit("expected exactly one upstream LocalizedStrings::CopyTo")
start = matches[0].start()
opening = text.index("{", matches[0].end())
depth = 1
end = opening + 1
while end < len(text) and depth:
    if text[end] == "{":
        depth += 1
    elif text[end] == "}":
        depth -= 1
    end += 1
if depth:
    raise SystemExit("unterminated upstream CopyTo body")
method = text[start:end]
if "for each" not in method or "arrayObj[index] = pair;" not in method:
    raise SystemExit("upstream CopyTo implementation changed; inspect the probe context")
output.write_text("""// The collection wrapper is a probe; CopyTo below is unchanged upstream source.
using namespace System;
using namespace System::Collections::Generic;
using namespace System::Globalization;
public ref class WpfLocalizedCopy {
    IEnumerable<KeyValuePair<CultureInfo^, String^>>^ _source;
public:
    WpfLocalizedCopy(IEnumerable<KeyValuePair<CultureInfo^, String^>>^ source) : _source(source) {}
    IEnumerator<KeyValuePair<CultureInfo^, String^>>^ GetEnumerator() { return _source->GetEnumerator(); }
""" + method + "\n};\n", encoding="utf-8")
print("upstream LocalizedStrings::CopyTo retained unchanged")
