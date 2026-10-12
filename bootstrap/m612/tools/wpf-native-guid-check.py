#!/usr/bin/env python3
"""Stage the original WPF Guid-to-native-GUID helper with a native byte check."""
from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
text = (source / "src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper/Common.h").read_text()
begin = text.index("    __declspec(noinline) static _GUID ToGUID(")
opening = text.index("{", begin)
closing = text.index("}", opening)
method = text[begin:closing + 1]
output.write_text("""// Original WPF helper; a public probe checks its native result bytes.
#include <windows.h>
#include <vcclr.h>
extern "C" int NativeGuidCheck(const void*);
public ref class WpfNativeGuid abstract sealed {
public:
""" + method + """
    static int Verify(System::Guid guid) {
        _GUID native = ToGUID(guid);
        System::GC::Collect();
        return NativeGuidCheck(&native);
    }
};
""")
