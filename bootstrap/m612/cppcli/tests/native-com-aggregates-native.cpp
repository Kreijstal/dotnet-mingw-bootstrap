// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "native-com-aggregates.hpp"
#include <objidl.h>
class Aggregates final : public IAggregates {
    LONG references=1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** result) override { *result=nullptr; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override { auto result=InterlockedDecrement(&references); if(!result) delete this; return result; }
    int STDMETHODCALLTYPE Mix(ByteArg a,WordArg b,FloatArg c,PairArg d,double e,int f) override {
        bool valid=a.Value==0xa5 && b.Value==0xf123 && c.Value==1.25f &&
            d.Low==0x89abcdef && d.High==0xfedcba98 && e==-2.5 && f==42;
        a.Value=0; d.Low=0;
        return valid ? 73 : -1;
    }
    HRESULT STDMETHODCALLTYPE SeekValue(LARGE_INTEGER value,ULARGE_INTEGER* output) override {
        output->QuadPart=static_cast<ULONGLONG>(value.QuadPart);
        return value.QuadPart==-5000000002LL ? S_FALSE : E_INVALIDARG;
    }
};
extern "C" __declspec(dllexport) void* create_aggregates() { return static_cast<IAggregates*>(new Aggregates()); }
extern "C" __declspec(dllexport) long long native_stream_seek() {
    IStream* stream=nullptr;
    if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))) return -1;
    LARGE_INTEGER position; position.QuadPart=1234567LL;
    ULARGE_INTEGER result;
    HRESULT status=stream->Seek(position,STREAM_SEEK_SET,&result);
    stream->Release();
    return SUCCEEDED(status) ? static_cast<long long>(result.QuadPart) : -1;
}
