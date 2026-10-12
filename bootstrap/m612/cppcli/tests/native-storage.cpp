// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
#include <vcclr.h>
using namespace System;
extern "C" int NativeGuidCheck(const void*);
using NativeIntRef = int&;
public ref class NativeStorage abstract sealed {
public:
    static long long ReadHidden(FieldsApi::WithHiddenValue& value) { GC::Collect(); return value.Read(); }
    static long long Hidden() { FieldsApi::WithHiddenValue value(42LL); return ReadHidden(value); }
    static _GUID ToGUID(Guid& guid) {
        array<Byte>^ bytes = guid.ToByteArray();
        pin_ptr<Byte> data = &bytes[0];
        return *(_GUID*)data;
    }
    static void AddFive(int& value) { GC::Collect(); value += 5; }
    static int Forward(int& value) { AddFive(value); return value; }
    static int ForwardAlias(NativeIntRef value) { AddFive(value); return value; }
    static int NativeAlias() { int value = 5; NativeIntRef alias = value; return ForwardAlias(alias); }
    static int NativeReference() {
        int value = 7;
        int& alias = value;
        return Forward(alias);
    }
    static int Check(Guid guid) {
        _GUID first = ToGUID(guid);
        _GUID second = first;
        _GUID* pointer = &second;
        *pointer = first;
        GC::Collect();
        return NativeGuidCheck(pointer);
    }
    static void IncrementPinned(array<Int32>^ values) {
        pin_ptr<Int32> item = &values[0];
        AddFive(*item);
    }
};
