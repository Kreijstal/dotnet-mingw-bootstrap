// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public interface class IObjectItem { int Read(); };
public ref class ObjectItem : IObjectItem {
public:
    int Value;
    ObjectItem(int value):Value(value) {}
    virtual int Read() { return Value; }
};
public ref class ObjectHolder {
public:
    Object^ Field;
    property Object^ Current;
};
public ref class InterfaceObjects abstract sealed {
public:
    static int Reads;
    static IObjectItem^ Operand(IObjectItem^ value) { ++Reads; GC::Collect(); return value; }
    static Object^ Accept(Object^ value) { GC::Collect(); return value; }
    static Object^ Identity(IObjectItem^ value) { Object^ result=Operand(value); return result; }
    static Object^ Argument(IObjectItem^ value) { return Accept(Operand(value)); }
    static Object^ Return(IObjectItem^ value) { return value; }
    static Object^ Field(ObjectHolder^ holder,IObjectItem^ value) { holder->Field=Operand(value); return holder->Field; }
    static Object^ Property(ObjectHolder^ holder,IObjectItem^ value) { holder->Current=Operand(value); return holder->Current; }
    static Object^ Imported(IDisposable^ value) { return Accept(value); }
    static Object^ ImportedReturn(IDisposable^ value) { return value; }
    static int Select(Object^ value) { return 1; }
    static int Select(IObjectItem^ value) { return 2; }
    static int Typed(IObjectItem^ value) { return Select(value); }
};
