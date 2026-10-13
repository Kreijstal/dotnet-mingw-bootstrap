// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
using namespace System::Threading;
public value struct StaticData { int Value; String^ Text; };
public ref class StaticLocals {
public:
    static int Initializations;
    static int Attempts;
    int Value;
    StaticLocals(int value) : Value(value) {}
    static int Initialize(int seed) {
        Interlocked::Increment(Initializations); Thread::Sleep(5); GC::Collect(); return seed;
    }
    static int Cached(int seed) { static int value=Initialize(seed); return value; }
    int Instance() { static int value=Value; return value; }
    static int RetryInitialize() {
        if(Interlocked::Increment(Attempts)==1) throw gcnew ArgumentException("retry");
        return 79;
    }
    static int Retry() { static int value=RetryInitialize(); return value; }
    static String^ Text(String^ seed) { static String^ value=seed; return value; }
    static int Count() { static int value; return ++value; }
    static int% Address() { static int value=80; return value; }
    static StaticData Make(String^ text) { StaticData value; value.Value=81; value.Text=text; return value; }
    static StaticData% Data(String^ seed) { static StaticData value=Make(seed); return value; }
    static int Scoped(bool first) {
        if(first) { static int value=82; return ++value; }
        else { static int value=84; return ++value; }
    }
};
