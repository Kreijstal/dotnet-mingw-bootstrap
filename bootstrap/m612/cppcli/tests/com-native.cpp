#include <windows.h>
#include <unknwn.h>
#include <cstdint>
static const GUID probe_iid = {0x138ef579,0x12a0,0x4b01,{0x89,0x13,0x67,0x55,0x42,0x95,0x8c,0xbd}};
static const GUID translated_iid = {0x271d54c4,0x1be3,0x4f9f,{0xac,0xb7,0x82,0x42,0xa2,0xbb,0x9a,0x34}};
static LONG live;
struct IProbe : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Read(uint64_t value, BOOL* result) = 0;
    virtual HRESULT STDMETHODCALLTYPE Fail() = 0;
    virtual HRESULT STDMETHODCALLTYPE WriteAddress(void** result) = 0;
};
struct ITranslated : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetValue(int* result) = 0;
    virtual HRESULT STDMETHODCALLTYPE FailWithoutSig() = 0;
};
class Probe final : public IProbe, public ITranslated {
    LONG references = 1;
public:
    Probe() { InterlockedIncrement(&live); }
    ~Probe() { InterlockedDecrement(&live); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, probe_iid)) *result = static_cast<IProbe*>(this);
        else if (IsEqualIID(iid, translated_iid)) *result = static_cast<ITranslated*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override {
        auto count = InterlockedDecrement(&references);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Read(uint64_t value, BOOL* result) override {
        if (!result) return E_POINTER;
        *result = value == UINT64_C(0xf123456789abcdef) ? 7 : 0;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Fail() override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE WriteAddress(void** result) override { if (!result) return E_POINTER; *result = static_cast<IProbe*>(this); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetValue(int* result) override { if (!result) return E_POINTER; *result = 42; return S_OK; }
    HRESULT STDMETHODCALLTYPE FailWithoutSig() override { return E_ACCESSDENIED; }
};
extern "C" __declspec(dllexport) void* create_probe() { return static_cast<IProbe*>(new Probe()); }
extern "C" __declspec(dllexport) int live_probes() { return live; }
struct IManagedCallback : IUnknown { virtual int STDMETHODCALLTYPE Value(int input) = 0; };
extern "C" __declspec(dllexport) int call_managed_callback(void* callback, int value) {
    return static_cast<IManagedCallback*>(callback)->Value(value);
}
