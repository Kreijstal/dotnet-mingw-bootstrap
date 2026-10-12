// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#pragma once
#include <windows.h>
#include <dwrite.h>
#pragma pack(push,1)
struct BitFields {
    unsigned char Guard;
    signed long long Signed:19;
    unsigned long long Unsigned:17;
    unsigned long long Tail:28;
    unsigned char Sentinel;
};
#pragma pack(pop)
struct ByteBits { unsigned char First:3; unsigned char Second:5; };
struct BoolBits { bool First:1; bool Second:1; };
struct VolatileBits { volatile unsigned char First:2; volatile unsigned char Second:6; };
#ifdef CPPCLI_NATIVE_BUILD
extern "C" __declspec(dllexport) int NativeBitFieldsCheck(const void*,const void*,const void*,const void*);
#else
extern "C" int NativeBitFieldsCheck(const void*,const void*,const void*,const void*);
#endif
