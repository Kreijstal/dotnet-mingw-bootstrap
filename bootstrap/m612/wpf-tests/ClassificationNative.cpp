// SPDX-License-Identifier: MIT
#include <windows.h>
#include <unicode/uchar.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <atomic>
#include "ClassificationEnums.h"
#pragma pack(push,1)
struct Attribute { uint8_t script,item; uint16_t flags; uint8_t breaking,bidi; int16_t line_break; };
#pragma pack(pop)
struct View { int16_t*** classes; Attribute* attributes; int count; };
using Query=HRESULT (WINAPI *)(View*);
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    HMODULE dll=LoadLibraryA(argv[1]);
    if(!dll) { std::printf("LoadLibrary error %lu\n",GetLastError()); return 1; }
    auto query=reinterpret_cast<Query>(GetProcAddress(dll,"WpfGetClassificationTables"));
    if(!query || query(nullptr)!=E_POINTER) return 1;
    View view{};
    if(query(&view)!=S_OK || !view.classes || !view.attributes || view.count<=0 || view.count>32767) return 1;
    const uint8_t bidi[]={0,1,3,5,7,2,6,11,17,18,19,12,13,4,14,15,16,8,9,9,9,9,9};
    for(int scalar=0;scalar<0x110000;++scalar) {
        int id=view.classes[scalar>>16][(scalar>>8)&255][scalar&255];
        if(id<0 || id>=view.count) { std::printf("class index out of range U+%X\n",scalar); return 1; }
        const auto& value=view.attributes[id];
        auto cat=u_charType(scalar);
        bool digit=cat==U_DECIMAL_DIGIT_NUMBER;
        if(value.script>=WPF_ScriptID_Max || value.item>=WPF_ItemClass_MaxClass ||
           value.bidi!=bidi[u_charDirection(scalar)] ||
           value.line_break!=u_getIntPropertyValue(scalar,UCHAR_LINE_BREAK) ||
           bool(value.flags&WPF_CharacterAttributeFlags_CharacterDigit)!=digit ||
           bool(value.flags&WPF_CharacterAttributeFlags_CharacterLetter)!=bool(u_isalpha(scalar)) ||
           bool(value.flags&WPF_CharacterAttributeFlags_CharacterSpace)!=bool(u_isUWhiteSpace(scalar)) ||
           bool(value.flags&WPF_CharacterAttributeFlags_CharacterIdeo)!=bool(u_hasBinaryProperty(scalar,UCHAR_IDEOGRAPHIC)) ||
           bool(value.flags&WPF_CharacterAttributeFlags_CharacterExtended)!=(scalar>0xFFFF)) {
            std::printf("Unicode property mismatch U+%X\n",scalar); return 1;
        }
        if((value.flags&WPF_CharacterAttributeFlags_CharacterFastText) &&
           (value.flags&WPF_CharacterAttributeFlags_CharacterComplex)) return 1;
    }
    std::atomic<bool> stable{true};
    std::array<std::thread,8> threads;
    for(auto& thread:threads) thread=std::thread([&]{
        for(int n=0;n<100;++n) {
            View other{};
            if(query(&other)!=S_OK || other.classes!=view.classes || other.attributes!=view.attributes || other.count!=view.count) stable=false;
        }
    });
    for(auto& thread:threads) thread.join();
    std::printf("classification ABI: attribute=%zu, view=%zu, count=%d; all 1114112 Unicode values and concurrent lifetime %s\n",sizeof(Attribute),sizeof(View),view.count,stable?"PASS":"FAIL");
    return stable?0:1;
}
