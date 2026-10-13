// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
using namespace System::Runtime::InteropServices;
public value struct PinnedPoint { int x; int y; };
[StructLayout(LayoutKind::Explicit,Size=32)]
public value struct PinnedWide { [FieldOffset(0)] int x; [FieldOffset(20)] int y; };
extern "C" int ValuePinProbe(void* pointer,int kind);
public ref class ValuePins abstract sealed {
public:
    static int Points(array<PinnedPoint>^ values) {
        pin_ptr<PinnedPoint> pin=&values[0];
        PinnedPoint* raw=pin;
        GC::Collect();
        int result=ValuePinProbe(raw,0);
        pin[1].x+=3;
        ++pin; pin->y+=4;
        return result;
    }
    static int Wide(array<PinnedWide>^ values) {
        pin_ptr<PinnedWide> pin=&values[0];
        PinnedWide* raw=static_cast<PinnedWide*>(pin);
        GC::Collect();
        int result=ValuePinProbe(raw,1);
        pin[1].x+=3;
        ++pin; pin->y+=4;
        return result;
    }
    static void Interior(array<PinnedWide>^ values) {
        interior_ptr<PinnedWide> pointer=&values[0];
        GC::Collect(); pointer[2].x=50;
        pointer+=2; GC::Collect(); pointer->y=60;
    }
};
