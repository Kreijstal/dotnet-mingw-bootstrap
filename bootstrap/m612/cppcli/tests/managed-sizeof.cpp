// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
using namespace System::Runtime::InteropServices;
public value struct SizePoint { int x; int y; };
[StructLayout(LayoutKind::Explicit,Size=32)]
public value struct SizeWide { [FieldOffset(0)] int x; [FieldOffset(20)] int y; };
public value struct SizeReferences { int Value; String^ Text; };
public ref class ManagedSizeOf abstract sealed {
public:
    static int Calls;
    static SizeWide Create() { ++Calls; SizeWide value; return value; }
    static unsigned long long Point() { return sizeof(SizePoint); }
    static unsigned long long Wide() { return sizeof(SizeWide); }
    static unsigned long long References() { return sizeof(SizeReferences); }
    static unsigned long long Unevaluated() { return sizeof(Create()); }
    static unsigned long long Bytes(int count) { return count*sizeof(SizeWide); }
};
