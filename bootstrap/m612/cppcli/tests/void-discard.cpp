// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
struct NativeValue { volatile int Field; };
public value struct ManagedValue { int Field; };
public ref class VoidDiscard abstract sealed {
public:
    static int Calls;
    static int Next() { return ++Calls; }
    static void Visit() { ++Calls; }
    static int Throw() { ++Calls; throw gcnew InvalidOperationException("discard operand"); }
    static void Check() {
        unsigned version=0x00010000;
        (void)version;
        static_cast<void>(Next());
        (void)Visit();
        (void)(void)Next();
        (void)Next();
        (void)Next();
        NativeValue native={123};
        ManagedValue managed;
        managed.Field=42;
        (void)native.Field;
        (void)managed;
        (void)(&version);
        (void)gcnew Object();
        bool caught=false;
        try { (void)Throw(); }
        catch(InvalidOperationException^) { caught=true; }
        GC::Collect();
        if(Calls!=6 || !caught || native.Field!=123 || managed.Field!=42)
            throw gcnew InvalidOperationException("discard effects");
    }
};
int main() {
    VoidDiscard::Check();
    Console::WriteLine("void discard casts: original version idiom, side effects, nested void, volatile/value/pointer/handle operands and exceptions PASS");
    return 0;
}
