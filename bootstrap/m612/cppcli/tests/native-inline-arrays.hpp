// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#pragma once
struct InlinePair { int Low; long long High; };
#pragma pack(push,1)
struct InlineArrays {
    unsigned char Tag;
    unsigned int Range[2];
    long long Wide[2];
    InlinePair Pairs[2];
    volatile unsigned short Flags[2];
    InlinePair* Pointers[2];
};
#pragma pack(pop)
#ifdef CPPCLI_NATIVE_BUILD
extern "C" __declspec(dllexport) int NativeInlineArraysCheck(const void*);
#else
extern "C" int NativeInlineArraysCheck(const void*);
#endif
