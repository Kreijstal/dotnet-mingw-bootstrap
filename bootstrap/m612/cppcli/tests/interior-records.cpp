// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
using namespace System;
#pragma pack(push,1)
struct PackedNative { Byte Tag; long long Wide; int Values[3]; };
#pragma pack(pop)
public ref class InteriorRecords abstract sealed {
public:
    static int Indices;
    static int Index(int value) { ++Indices; GC::Collect(); return value; }
    static int Bitmap(array<Byte>^ bytes) {
        interior_ptr<Byte> data=&bytes[0];
        interior_ptr<BITMAPINFO> bitmap=(interior_ptr<BITMAPINFO>)data;
        GC::Collect();
        bitmap->bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bitmap->bmiHeader.biWidth=17;
        bitmap->bmiColors[Index(0)].rgbRed=41;
        bitmap->bmiColors[Index(0)].rgbGreen=42;
        bitmap->bmiColors[Index(0)].rgbBlue=43;
        bitmap->bmiColors[Index(0)].rgbRed+=2;
        int old=bitmap->bmiColors[Index(0)].rgbGreen++;
        int now=++bitmap->bmiColors[Index(0)].rgbBlue;
        return bitmap->bmiHeader.biWidth+bitmap->bmiColors[0].rgbRed+old+now;
    }
    static long long Packed(array<Byte>^ bytes) {
        interior_ptr<Byte> data=&bytes[0];
        interior_ptr<PackedNative> record=(interior_ptr<PackedNative>)data;
        record->Tag=9;
        record->Wide=5000000002LL;
        record->Values[Index(1)]=23;
        int% alias=record->Values[1];
        GC::Collect();
        alias+=2;
        return record->Wide+record->Values[1];
    }
    static int Walk(array<Byte>^ bytes) {
        interior_ptr<BITMAPINFO> bitmap=(interior_ptr<BITMAPINFO>)&bytes[0];
        interior_ptr<DWORD> masks=(interior_ptr<DWORD>)&bitmap->bmiColors[0];
        masks[0]=10;
        interior_ptr<DWORD> previous=masks++;
        GC::Collect();
        masks[0]=20;
        masks+=Index(1);
        masks[0]=30;
        interior_ptr<DWORD> last=masks--;
        --masks;
        ++masks;
        masks-=Index(1);
        return previous[0]+last[0]+masks[0];
    }
};
