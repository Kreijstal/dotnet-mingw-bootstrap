// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <malloc.h>
struct NativePair { int Low; long long High; };
static_assert(sizeof(NativePair)==16);
extern "C" __declspec(dllexport) int NativeArrayCheck(const void* pointer, unsigned int count) {
    if(count!=3||_msize(const_cast<void*>(pointer))<sizeof(NativePair)*count) return 1;
    auto* values=static_cast<const NativePair*>(pointer);
    for(unsigned i=0;i<count;++i) if(values[i].Low!=int(i+1)||values[i].High!=5000000002LL+i) return 2;
    return 0;
}
