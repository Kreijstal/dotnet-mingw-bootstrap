// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public ref class BaseValueState abstract sealed {
public:
    static int Calls;
    static IntPtr Build() { ++Calls; GC::Collect(); return IntPtr(0x1234); }
    static IntPtr Fail() { ++Calls; throw gcnew ArgumentException("base value"); }
};
public ref class PointerBase {
public:
    IntPtr Pointer;
    int Order;
    PointerBase(IntPtr value):Pointer(value),Order(BaseValueState::Calls) {}
};
public ref class PointerDerived : PointerBase {
public:
    int After;
    PointerDerived():PointerBase(BaseValueState::Build()),After(BaseValueState::Calls) { GC::Collect(); }
};
public ref class PointerFailure : PointerBase {
public:
    PointerFailure():PointerBase(BaseValueState::Fail()) { ++BaseValueState::Calls; }
};
