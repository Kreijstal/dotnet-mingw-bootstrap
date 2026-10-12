#!/usr/bin/env python3
"""Stage WPF's native-pointer CriticalHandle template and original accessors."""
from pathlib import Path
import sys
source, output = map(Path, sys.argv[1:])
folder=source/'src/Microsoft.DotNet.Wpf/src/DirectWriteForwarder/CPP/DWriteWrapper'
header=(folder/'NativePointerWrapper.h').read_text()
begin=header.index('    template <class T>')
end=header.index('    template <class T>',begin+1)
declaration=header[begin:end]
implementation=(folder/'NativePointerWrapper.cpp').read_text()
begin=implementation.index('    template <class T>')
end=implementation.index('    template <class T>\n    NativeIUnknownWrapper',begin)
methods=implementation[begin:end]
output.write_text("""// Original WPF base template/accessors; the probe release method supplies observable cleanup.
using namespace System;
using namespace System::Runtime::InteropServices;
namespace MS { namespace Internal { namespace Text { namespace TextInterface { namespace Generics {
"""+declaration+methods+"""
}}}}}
public ref class WpfPointerProbe : MS::Internal::Text::TextInterface::Generics::NativePointerCriticalHandle<int> {
    static int _releases;
protected:
    virtual bool ReleaseHandle() override { handle = IntPtr::Zero; ++_releases; return true; }
public:
    WpfPointerProbe(IntPtr pointer) : NativePointerCriticalHandle<int>(pointer.ToPointer()) {}
    IntPtr Address() { return IntPtr(Value); }
    static int Releases() { return _releases; }
};
""")
