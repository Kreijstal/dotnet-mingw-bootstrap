// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#define CPPCLI_NATIVE_BUILD
#include "native-inline-arrays.hpp"
#include <cstddef>
static_assert(sizeof(InlinePair)==16);
static_assert(offsetof(InlineArrays,Range)==1);
static_assert(offsetof(InlineArrays,Wide)==9);
extern "C" __declspec(dllexport) int NativeInlineArraysCheck(const void* pointer) {
    auto* value=static_cast<const InlineArrays*>(pointer);
    if(value->Tag!=7 || value->Range[0]!=10 || value->Range[1]!=23) return 1;
    if(value->Wide[0]!=0 || value->Wide[1]!=5000000002LL) return 2;
    if(value->Pairs[1].Low!=42 || value->Pairs[1].High!=5000000004LL) return 3;
    if(value->Flags[0]!=0 || value->Flags[1]!=3) return 4;
    if(value->Pointers[0]!=&value->Pairs[1] || value->Pointers[1]!=nullptr) return 5;
    return 0;
}
