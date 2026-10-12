// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
#include <unknwn.h>
#include <vcclr.h>
using namespace System;
struct IProbe : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Read(unsigned long long value, BOOL* result) = 0;
    virtual HRESULT STDMETHODCALLTYPE Fail() = 0;
    virtual HRESULT STDMETHODCALLTYPE WriteAddress(void** result) = 0;
};
extern "C" void* create_probe();
extern "C" int live_probes();
struct IArithmetic : IUnknown {
    virtual double STDMETHODCALLTYPE Blend(float a, double b, unsigned long long c, int d, unsigned short e) = 0;
    virtual void STDMETHODCALLTYPE Write(int* output) = 0;
};
extern "C" void* create_arithmetic();
IProbe* SingleReceiver(IProbe* value, int% calls) { ++calls; return value; }
public ref class NativeCom abstract sealed {
public:
    static _GUID NativeGuid(Guid value) {
        array<Byte>^ bytes = value.ToByteArray();
        pin_ptr<Byte> data = &bytes[0];
        return *(_GUID*)data;
    }
    static int Verify() {
        IProbe* probe = static_cast<IProbe*>(create_probe());
        if (probe == nullptr || live_probes() != 1) return 1;
        if (probe->AddRef() != 2 || probe->Release() != 1) return 2;
        BOOL result = 0;
        int calls = 0;
        if (SingleReceiver(probe, calls)->Read(0xf123456789abcdefULL, &result) != S_FALSE || result != 7 || calls != 1) return 3;
        if (probe->Fail() != E_ACCESSDENIED) return 4;
        void* returned = nullptr;
        if (probe->WriteAddress(&returned) != S_OK || returned != probe) return 5;
        _GUID iid = NativeGuid(Guid("138ef579-12a0-4b01-8913-675542958cbd"));
        returned = nullptr;
        if (probe->QueryInterface(iid, &returned) != S_OK || returned != probe) return 6;
        if (static_cast<IUnknown*>(returned)->Release() != 1) return 7;
        if (probe->Release() != 0 || live_probes() != 0) return 8;
        IArithmetic* arithmetic = static_cast<IArithmetic*>(create_arithmetic());
        if (arithmetic->Blend(1.5f, 2.25, 5000000002ULL, 4, 6) != 13.75) return 9;
        int output = 0;
        arithmetic->Write(&output);
        if (output != 42 || arithmetic->Release() != 0) return 10;
        return 0;
    }
};
int main() {
    int result = NativeCom::Verify();
    if (result != 0) { Console::WriteLine(result); return 1; }
    Console::WriteLine("native COM vtable dispatch, inheritance, GUID reference, HRESULT, outputs and receiver evaluation OK");
    return 0;
}
