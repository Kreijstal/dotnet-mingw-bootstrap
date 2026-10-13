// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public ref class ProbeProperty {
public:
    Object^ Data;
    int Gets;
    int Sets;
    property Object^ Value { Object^ get() { ++Gets; GC::Collect(); return Data; } void set(Object^ value) { ++Sets; GC::Collect(); Data=value; } }
    static String^ Read(ProbeProperty% value) { return (String^)value.Value; }
    static void Write(ProbeProperty% value,Object^ text) { value.Value=text; }
    static void Clear(ProbeProperty% value) { value.Value=nullptr; }
    static Object^ Replace(ProbeProperty^% value) { value=gcnew ProbeProperty(); return "replacement"; }
    static void ReplaceDuringWrite(ProbeProperty% value,ProbeProperty^% handle) { value.Value=Replace(handle); }
};
public ref class NativeNullProperty {
public:
    void* Data;
    int Sets;
    property void* Value { void* get() { return Data; } void set(void* value) { ++Sets; Data=value; } }
    static void Clear(NativeNullProperty^ value) { value->Value=nullptr; }
};
