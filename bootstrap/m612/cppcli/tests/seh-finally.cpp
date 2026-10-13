// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
using namespace System::Threading;
public ref class SehFinally {
public:
    static int Cleanups;
    int Value;
    static int Return(int value) { __try { return value; } __finally { ++Cleanups; GC::Collect(); } }
    static int Leave() { int value=1; __try { value=2; __leave; value=99; } __finally { value+=3; ++Cleanups; } return value; }
    static int Loop() {
        int value=0;
        for(int i=0;i<4;++i) {
            __try { if(i==1) continue; if(i==3) break; value+=i; }
            __finally { ++Cleanups; }
        }
        return value;
    }
    static void Throw(Object^ gate) {
        Monitor::Enter(gate);
        __try { throw gcnew ArgumentException("seh cleanup"); }
        __finally { ++Cleanups; Monitor::Exit(gate); }
    }
    static int Nested() {
        __try { __try { return 6; } __finally { Cleanups+=10; } }
        __finally { Cleanups+=100; }
    }
    int% Address() { __try { return Value; } __finally { ++Cleanups; GC::Collect(); } }
};
