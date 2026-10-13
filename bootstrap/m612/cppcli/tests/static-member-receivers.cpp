// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public ref class StaticReceiver {
public:
    static int Calls;
    static int Value=7;
    static property int Number { int get() { return Value+1; } }
    static StaticReceiver^ Receiver(StaticReceiver^ value) { ++Calls; GC::Collect(); return value; }
    static int Field(StaticReceiver^ value) { return Receiver(value)->Value; }
    static int Property(StaticReceiver^ value) { return Receiver(value)->Number; }
    static IntPtr NativeValue() { ++Calls; GC::Collect(); return IntPtr::Zero; }
    static int NativeSize() { return NativeValue().Size; }
    static int Size(IntPtr value) { return value.Size; }
};
