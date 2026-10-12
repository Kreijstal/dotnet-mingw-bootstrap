#!/usr/bin/env python3
"""Stage the complete WPF localized-string enumerator in an array-backed owner."""
from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
folder = source / "src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper"
header = (folder / "LocalizedStrings.h").read_text()
implementation = (folder / "LocalizedStrings.cpp").read_text()

def block(text, begin):
    opening = text.index("{", begin)
    depth, end = 1, opening + 1
    while depth:
        if text[end] == "{": depth += 1
        elif text[end] == "}": depth -= 1
        end += 1
    return text[begin:end], end

begin = header.index("            ref struct LocalizedStringsEnumerator :")
enumerator, end = block(header, begin)
if header[end] != ";": raise SystemExit("upstream enumerator terminator changed")
methods = []
for marker in ("    __declspec(noinline) KeyValuePair<CultureInfo^, String^> LocalizedStrings::LocalizedStringsEnumerator::Current::get()",
               "    Object^ LocalizedStrings::LocalizedStringsEnumerator::Current2::get()"):
    method, _ = block(implementation, implementation.index(marker))
    methods.append(method)
messages = (folder / "LocalizedErrorMsgs.cpp").as_posix()
output.write_text("""// Original WPF nested enumerator and getters; the owner supplies managed test arrays.
using namespace System;
using namespace System::Collections::Generic;
using namespace System::Globalization;
typedef long long INT64;
typedef unsigned int UINT32;
#include """ + '"' + messages + '"\n' + """
namespace MS { namespace Internal { namespace Text { namespace TextInterface {
public ref class LocalizedStrings {
    array<CultureInfo^>^ _keys;
    array<String^>^ _values;
    property UINT32 StringsCount { UINT32 get() { return _values->Length; } }
    property array<CultureInfo^>^ KeysArray { array<CultureInfo^>^ get() { return _keys; } }
    property array<String^>^ ValuesArray { array<String^>^ get() { return _values; } }
public:
    LocalizedStrings(array<CultureInfo^>^ keys, array<String^>^ values) : _keys(keys), _values(values) {}
""" + enumerator + ";\n" + """
    IEnumerator<KeyValuePair<CultureInfo^, String^>>^ Open() { return gcnew LocalizedStringsEnumerator(this); }
};
""" + "\n".join(methods) + "\n}}}}\n")
