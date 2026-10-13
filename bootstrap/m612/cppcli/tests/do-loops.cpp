// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public ref class DoLoops abstract sealed {
public:
    static int Conditions;
    static int Cleanups;
    static bool Condition(int value) { ++Conditions; GC::Collect(); return value<3; }
    static int Once() { int value=0; do { ++value; } while(false); return value; }
    static int Continue() { int value=0; do { ++value; if(value==1) continue; } while(Condition(value)); return value; }
    static int Break() { int value=0; do { ++value; break; } while(Condition(value)); return value; }
    static int Protected() {
        int value=0;
        do { try { ++value; if(value==1) continue; if(value==3) break; } finally { ++Cleanups; } } while(Condition(value));
        return value;
    }
    static int Nested() { int result=0; int outer=0; do { int inner=0; do { ++inner; ++result; } while(inner<2); ++outer; } while(outer<3); return result; }
    static int Return() { do { try { return 7; } finally { ++Cleanups; } } while(true); }
};
