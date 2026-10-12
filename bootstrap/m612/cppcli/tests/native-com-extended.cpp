// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
#include <unknwn.h>
struct IArithmetic : IUnknown {
    virtual double STDMETHODCALLTYPE Blend(float a, double b, unsigned long long c, int d, unsigned short e) = 0;
    virtual void STDMETHODCALLTYPE Write(int* output) = 0;
};
class Arithmetic final : public IArithmetic {
    LONG references = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** result) override { if (result) *result = nullptr; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override { auto result = InterlockedDecrement(&references); if (!result) delete this; return result; }
    double STDMETHODCALLTYPE Blend(float a, double b, unsigned long long c, int d, unsigned short e) override {
        return c == 5000000002ULL ? a + b + d + e : -1.0;
    }
    void STDMETHODCALLTYPE Write(int* output) override { *output = 42; }
};
extern "C" __declspec(dllexport) void* create_arithmetic() { return static_cast<IArithmetic*>(new Arithmetic()); }
