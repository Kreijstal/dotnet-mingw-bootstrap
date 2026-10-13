// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public value struct PropertyData {
    int Value;
    String^ Text;
    property int Number { int get() { return Value; } void set(int value) { GC::Collect(); Value=value; } }
    property String^ Label { String^ get() { return Text; } void set(String^ value) { GC::Collect(); Text=value; } }
};
public ref class PropertyOwner { public: PropertyData Data; };
public ref class ValuePropertyLocations abstract sealed {
public:
    static int Indices;
    static int Index() { ++Indices; GC::Collect(); return 1; }
    static void Ref(PropertyData% value,String^ text) { value.Number=7; value.Label=text; }
    static void Array(array<PropertyData>^ values,String^ text) { values[Index()].Number=8; values[1].Label=text; }
    static void Field(PropertyOwner^ owner,String^ text) { owner->Data.Number=9; owner->Data.Label=text; }
    static PropertyData Copy(PropertyData value) { value.Number=10; return value; }
};
