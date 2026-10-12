// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
using namespace System::Runtime::InteropServices;
[StructLayout(LayoutKind::Explicit, Size=32)]
public value struct PointerCell {
    [FieldOffset(0)] int X;
    [FieldOffset(20)] int Y;
};
public ref class ValuePointerArithmetic abstract sealed {
public:
    static PointerCell* Advance(PointerCell* pointer, int offset) { return pointer+offset; }
    static long long Distance(PointerCell* first, PointerCell* second) { return second-first; }
    static int Verify(PointerCell* pointer) {
        PointerCell* next=pointer+1;
        if(next-pointer!=1 || !(pointer<next) || next->X!=12 || next->Y!=102) return 1;
        ++next; if(next->X!=13 || next->Y!=103) return 2;
        if((next-=2)!=pointer) return 3;
        if(next++!=pointer || next->X!=12) return 4;
        pointer[2].Y=203;
        GC::Collect();
        return 0;
    }
};
