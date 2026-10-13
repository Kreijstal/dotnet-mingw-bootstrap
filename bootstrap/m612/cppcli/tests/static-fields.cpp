// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
namespace Example {
public ref class State abstract sealed {
    static Object^ _lock = gcnew Object();
    static int _seed = MakeSeed();
    static int _next = _seed + 2;
    static String^ _raw = "RAW";
    static System::String^ _unicode = L"東京 " L"😀";
    static int MakeSeed() { Console::WriteLine("initialize once"); return 40; }
public:
    static Object^ Lock() { return _lock; }
    static int Read() { return _next; }
    static String^ Raw() { return _raw; }
    static String^ Unicode() { return _unicode; }
    static void Change(int% value) { value += 1; }
    static void Increment() { Change(_next); }
};
public ref class Failing abstract sealed {
    static int _value = Fail();
    static int Fail() { throw gcnew InvalidOperationException(); }
public:
    static int Read() { return _value; }
};
public ref class Explicit {
    static int _seed = 40;
    static int _value = _seed + 2;
    static int _runs;
    static Explicit(void) { _value += 1; _runs += 1; }
public:
    int Snapshot;
    Explicit(int value) : Snapshot(value) {}
    static int Read() { return _value; }
    static int Runs() { return _runs; }
};
public ref class OutOfLine abstract sealed {
    static int _value = 41;
    static OutOfLine(void);
public:
    static int Read() { return _value; }
};
static OutOfLine::OutOfLine(void) { _value += 1; }
public value struct ValueInitialization {
    static int _value = 41;
    static ValueInitialization() { _value += 1; }
    static int Read() { return _value; }
};
public ref class FailingExplicit abstract sealed {
    static FailingExplicit() { throw gcnew InvalidOperationException(); }
public:
    static int Read() { return 0; }
};
}
