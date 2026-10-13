// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <wchar.h>
#include <string.h>
using namespace System;
int main() {
    wchar_t* raw=new wchar_t[6]();
    raw[0]=0x03bb; raw[1]=0xd83d; raw[2]=0xde80;
    if(wcslen(raw)!=3 || wcslen(raw+1)!=2 || wcslen(raw+3)!=0) return 1;
    raw[1]=0;
    if(wcslen(raw)!=1 || wcslen(raw+2)!=1) return 2;
    delete[] raw;
    wchar_t* names=new wchar_t[8]();
    names[0]='A'; names[2]='B'; names[3]='C'; names[5]=0x03bb;
    int count=0; size_t length=0;
    for(wchar_t* pointer=names; pointer && *pointer; pointer+=length+1) { length=wcslen(pointer); ++count; }
    delete[] names;
    if(count!=3) return 3;
    String^ text="λ🚀";
    pin_ptr<const wchar_t> pin=PtrToStringChars(text);
    for(int i=0;i<20;++i) {
        GC::Collect();
        if(wcslen(static_cast<const wchar_t*>(pin))!=3) return 4;
    }
    array<unsigned char>^ bytes=gcnew array<unsigned char>(16);
    for(int i=0;i<16;++i) bytes[i]=0x5a;
    pin_ptr<unsigned char> buffer=&bytes[0];
    unsigned char* start=static_cast<unsigned char*>(buffer)+4;
    if(memset(start,0x1ff,6)!=start) return 5;
    GC::Collect();
    for(int i=0;i<16;++i) if(bytes[i]!=(i>=4 && i<10 ? 0xff : 0x5a)) return 6;
    if(memset(start,0,0)!=start || bytes[4]!=0xff) return 7;
    memset(start,0,6);
    for(int i=0;i<16;++i) if(bytes[i]!=(i>=4 && i<10 ? 0 : 0x5a)) return 8;
    Console::WriteLine("CRT wcslen/memset: UTF-16/surrogate/NUL/MULTI_SZ, pinned text/bytes, native return pointers, truncation/bounds/zero count and GC PASS");
    return 0;
}
