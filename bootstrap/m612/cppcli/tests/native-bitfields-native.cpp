// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#define CPPCLI_NATIVE_BUILD
#include "native-bitfields.hpp"
static_assert(sizeof(BitFields)==10);
static_assert(sizeof(ByteBits)==1);
static_assert(sizeof(DWRITE_SHAPING_TEXT_PROPERTIES)==2);
extern "C" __declspec(dllexport) int NativeBitFieldsCheck(const void* first,const void* second,const void* third,const void* fourth) {
    auto* values=static_cast<const BitFields*>(first);
    auto* bytes=static_cast<const ByteBits*>(second);
    auto* text=static_cast<const DWRITE_SHAPING_TEXT_PROPERTIES*>(third);
    auto* vol=static_cast<const VolatileBits*>(fourth);
    if(values->Guard!=0xa5 || values->Sentinel!=0x5a || values->Signed!=-7 || values->Unsigned!=131071 || values->Tail!=0xabcdef0) return 1;
    if(bytes->First!=1 || bytes->Second!=31 || *reinterpret_cast<const unsigned char*>(bytes)!=249) return 2;
    if(text->isShapedAlone!=1 || text->canBreakShapingAfter!=1 || text->reserved!=123 || *reinterpret_cast<const unsigned short*>(text)!=989) return 3;
    if(vol->First!=3 || vol->Second!=42) return 4;
    return 0;
}
