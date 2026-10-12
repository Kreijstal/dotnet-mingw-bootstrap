#!/usr/bin/env python3
"""Retain Factory's reliability accessor declaration and IsInvalid body."""
from pathlib import Path
import sys
source, output = map(Path, sys.argv[1:])
folder = source / "src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper"
def region(text, start):
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth and end < len(text):
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    if depth:
        raise SystemExit("unterminated upstream Factory accessor")
    return text[start:end]
header = (folder / "Factory.h").read_text(encoding="utf-8")
cpp = (folder / "Factory.cpp").read_text(encoding="utf-8")
declaration = region(header, header.index("virtual property bool IsInvalid"))
method = region(cpp, cpp.index("bool Factory::IsInvalid::get()"))
if "ReliabilityContract" not in declaration or "return (_pFactory == NULL);" not in method:
    raise SystemExit("upstream Factory accessor changed; inspect probe context")
output.write_text("""#include <dwrite.h>
using namespace System;
using namespace System::Runtime::InteropServices;
using namespace System::Runtime::ConstrainedExecution;
public ref class FactoryProbe : CriticalHandle {
    IDWriteFactory* _pFactory;
protected:
    virtual bool ReleaseHandle() override { return true; }
public:
    FactoryProbe() : CriticalHandle(IntPtr::Zero), _pFactory(nullptr) {}
""" + declaration + "\n};\n" + method.replace("Factory::", "FactoryProbe::", 1) + "\n", encoding="utf-8")
print("upstream Factory reliability declaration and IsInvalid body retained")
