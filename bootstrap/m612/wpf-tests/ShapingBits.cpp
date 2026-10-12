// SPDX-License-Identifier: MIT
#include <windows.h>
#include <dwrite.h>
using namespace System;
extern "C" int WpfNativeShapingFill(DWRITE_SHAPING_TEXT_PROPERTIES*,UINT32,UINT32*);
extern "C" int WpfNativeShapedAlone(const DWRITE_SHAPING_TEXT_PROPERTIES*,UINT32);
int main() {
    DWRITE_SHAPING_TEXT_PROPERTIES* properties=new DWRITE_SHAPING_TEXT_PROPERTIES[6]();
    int* pfCanGlyphAlone=new int[6]();
    try {
        UINT32 textLength=6,glyphCount=0;
        UINT16* textProps=reinterpret_cast<UINT16*>(properties);
        if(WpfNativeShapingFill(properties,textLength,&glyphCount)!=S_OK || glyphCount==0 || glyphCount>32) return 1;
        GC::Collect();
#include "ShapingLoopOriginal.inc"
        for(UINT32 i=0;i<textLength;++i) if(pfCanGlyphAlone[i]!=WpfNativeShapedAlone(properties,i)) return 2;
        Console::WriteLine(glyphCount);
    } finally { delete[] pfCanGlyphAlone; delete[] properties; }
    Console::WriteLine("original WPF shaped-alone loop reads real DirectWrite GetGlyphs bitfields correctly across GC OK");
    return 0;
}
