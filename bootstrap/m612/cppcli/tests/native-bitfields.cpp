// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "native-bitfields.hpp"
using namespace System;
BitFields* Receiver(BitFields* value,int% order) { order=order*10+2; return value; }
long long Rhs(int% order) { order=order*10+1; GC::Collect(); return 2; }
int main() {
    BitFields* values=new BitFields();
    ByteBits* bytes=new ByteBits();
    DWRITE_SHAPING_TEXT_PROPERTIES* text=new DWRITE_SHAPING_TEXT_PROPERTIES();
    VolatileBits* vol=new VolatileBits();
    BoolBits* booleans=new BoolBits();
    try {
        values->Guard=0xa5; values->Sentinel=0x5a;
        values->Signed=-7; values->Unsigned=131071; values->Tail=0xabcdef0;
        if(values->Signed!=-7 || values->Unsigned!=131071 || values->Tail!=0xabcdef0) return 1;
        long long overflow=400000;
        if((values->Signed=overflow)!=-124288 || values->Signed!=-124288) return 2;
        values->Signed=-7; int order=0;
        if((Receiver(values,order)->Signed+=Rhs(order))!=-5 || order!=12 || values->Unsigned!=131071) return 3;
        values->Signed-=2;
        bytes->Second=31; bytes->First=7;
        if(bytes->First++!=7 || bytes->First!=0 || bytes->Second!=31) return 4;
        if(++bytes->First!=1 || bytes->Second!=31) return 5;
        if((bytes->First|=10)!=3 || (bytes->First-=2)!=1 || bytes->Second!=31) return 6;
        text->isShapedAlone=1; text->canBreakShapingAfter=1; text->reserved=123;
        if(text->isShapedAlone!=1 || text->reserved!=123) return 7;
        booleans->First=true; booleans->Second=false;
        booleans->Second|=3; if(!booleans->First || !booleans->Second) return 8;
        booleans->First=false; if(booleans->First || !booleans->Second) return 9;
        vol->Second=42; vol->First=2;
        if(vol->First++!=2 || vol->First!=3 || vol->Second!=42) return 10;
        GC::Collect();
        if(NativeBitFieldsCheck(values,bytes,text,vol)!=0) return 11;
        ByteBits initialized={5,17};
        if(initialized.First!=5 || initialized.Second!=17) return 12;
    } finally { delete booleans; delete vol; delete text; delete bytes; delete values; }
    Console::WriteLine("native bitfields: SDK layout, signed/cross-byte masks, narrow results, neighbor preservation, updates, volatile byte units and GC OK");
    return 0;
}
