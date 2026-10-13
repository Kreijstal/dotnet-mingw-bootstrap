// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
#include <unknwn.h>
struct ByteArg { unsigned char Value; };
struct WordArg { unsigned short Value; };
struct FloatArg { float Value; };
struct PairArg { unsigned int Low; unsigned int High; };
struct IAggregates : IUnknown {
    virtual int STDMETHODCALLTYPE Mix(ByteArg a, WordArg b, FloatArg c, PairArg d, double e, int f) = 0;
    virtual HRESULT STDMETHODCALLTYPE SeekValue(LARGE_INTEGER value, ULARGE_INTEGER* output) = 0;
};
