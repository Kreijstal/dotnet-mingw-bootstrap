// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public value struct PointerPoint {
    int X;
    long long Y;
    PointerPoint(int x, long long y) { X = x; Y = y; }
    void Move(int delta) { X += delta; }
};
extern "C" int NativePoint(PointerPoint*);
public ref class PointerHolder {
    PointerPoint* _pointer;
public:
    PointerHolder(PointerPoint* pointer) : _pointer(pointer) {}
    PointerPoint* Get() { return _pointer; }
};
public ref class ValuePointers abstract sealed {
public:
    static PointerPoint* Identity(PointerPoint* value) { return value; }
    static PointerPoint* Choose(PointerPoint* value, bool choose) { return choose ? value : nullptr; }
    static unsigned long long PredefinedAddress() { void* value = (void*)(long long)-2147483646LL; return (unsigned long long)value; }
    static PointerPoint^ Box(int value) { return gcnew PointerPoint(value, 99LL); }
    static int BoxRead(PointerPoint^ value) { return value->X; }
    static int Update(PointerPoint* pointer) {
        PointerPoint* alias = Identity(pointer);
        if (Choose(alias, false) != nullptr || Choose(alias, true) != alias) return -2;
        PointerHolder^ holder = gcnew PointerHolder(alias);
        alias = holder->Get();
        if (alias == nullptr) return -1;
        alias->X += 5;
        alias->Move(1);
        PointerPoint copy = *alias;
        copy.X += 1;
        *alias = copy;
        GC::Collect();
        return NativePoint(alias);
    }
    static int Stack() { PointerPoint value(5, 99LL); PointerPoint* pointer = &value; return Update(pointer); }
};
