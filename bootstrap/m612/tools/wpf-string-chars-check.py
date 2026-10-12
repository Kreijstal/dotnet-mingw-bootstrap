#!/usr/bin/env python3
"""Stage the original WPF string-interior-pointer helper in a public probe."""
from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
text = (source / "src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper/Common.h").read_text()
begin = text.index("    __declspec(noinline) const cli::interior_ptr<const System::Char> static GetPtrToStringChars(")
opening = text.index("{", begin)
closing = text.index("}", opening)
method = text[begin:closing + 1]
output.write_text("""// Original WPF helper; only its enclosing type is a public probe.
#include <vcclr.h>
public ref class WpfStringChars {
public:
""" + method + "\n};\n")
