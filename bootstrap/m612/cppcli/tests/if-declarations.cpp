// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public ref class IfDeclarations abstract sealed {
public:
    static int Calls;
    static String^ Pick(String^ text) { ++Calls; GC::Collect(); return text; }
    static int Number(int value) { ++Calls; return value; }
    static String^ Handle(String^ text) {
        if(String^ value=Pick(text)) { GC::Collect(); return value; }
        else { return value; }
    }
    static int Scalar(int input) { if(int value=Number(input)) return value+1; else return value-1; }
    static int Nested(int input) {
        if(int value=Number(input)) { if(int value=Number(3)) return value; else return value; }
        else return value;
    }
    static int Init(int input) {
        if(int first=Number(input); int second=Number(first+1)) return first+second;
        else return first-second;
    }
    static int Loop() { int total=0; for(int i=0;i<3;++i) if(int value=Number(i)) total+=value; else total-=1; return total; }
    static int Protected(int input) {
        try { if(int value=Number(input)) return value; else return value-1; }
        finally { ++Calls; }
    }
};
