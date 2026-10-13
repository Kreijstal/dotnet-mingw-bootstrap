// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public ref class Gotos abstract sealed {
public:
    static int Cleanups;
    static int Classify(array<int>^ values) {
        int flags=0;
        for(int i=0;i<values->Length;i++) if(values[i]!=1) goto Mixed;
        flags|=4;
Mixed: return flags;
    }
    static int Backward(int count) { int result=0; Again: ++result; if(result<count) goto Again; return result; }
    static int Protected() { try { goto Done; } finally { ++Cleanups; } Done: return 7; }
    static int Nested() { try { try { goto Done; } finally { Cleanups+=10; } } finally { Cleanups+=100; } Done: return 8; }
    static int Pin(array<int>^ values) { { pin_ptr<int> pin=&values[0]; *pin=9; goto Done; } Done: GC::Collect(); return values[0]; }
    static int WithinFinally() { try {} finally { goto Next; Cleanups=999; Next: ++Cleanups; } return Cleanups; }
};
