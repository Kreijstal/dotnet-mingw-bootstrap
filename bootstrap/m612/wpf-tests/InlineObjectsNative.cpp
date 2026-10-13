// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "LineServicesNative.h"
static std::wstring text=L"a\ufffcb\u2029";
static int after_first=2,before_second=2,error=0,formats=0,draws=0;
static LSPOINT object_origin{};
static void require(bool value,const char* why) { if(!value)throw std::runtime_error(why); }
static int WINAPI pap(void*,int,LsPap* value) { *value={};value->grpf=0x40;return 0; }
static int WINAPI props(void*,int,int,LsLineProps* value) { *value={};value->durLeft=4;return 0; }
static int WINAPI fetch(void*,int cp,int,void*,WCHAR* buffer,int,int* used,WCHAR**,int* count,int* hidden,LsChp* chp,uintptr_t* id) {
    if(cp<0 || cp>=static_cast<int>(text.size()))return LS_WrongGivenCp;
    *used=1;*count=1;*hidden=0;buffer[0]=text[cp];*chp={};chp->idObj=text[cp]==0xfffc?1:0xffff;*id=cp+5;return 0;
}
static int WINAPI metrics(void*,uint32_t,int,int,LsTxM* value) { *value={12,8,20,0};return 0; }
static int WINAPI widths(void*,uint32_t,int,WCHAR*,int count,int,int,int* values,int* total,int* fitted) { *total=10*count;*fitted=count;for(int i=0;i<count;++i)values[i]=10;return 0; }
static int WINAPI draw_text(void*,uint32_t,LSPOINT*,WCHAR*,int*,int,int,uint32_t,LSPOINT*,LsHeights*,int,LSRECT*) { return 0; }
static int WINAPI draw_object(void*,uint32_t id,LSPOINT* point,int flow,int width) {
    if(flow || width!=(text.size()==3?50:70) || id<5)return LS_ClientAbort;
    object_origin=*point;++draws;return error==3?LS_ClientAbort:0;
}
static int WINAPI format_object(void*,uint32_t id,int cp,int position,int margin,ObjDim* dim,int* first,int* used,int* before,int* after) {
    if(id!=static_cast<uint32_t>(cp+5) || margin!=LS_IdealInfiniteWidth || position<4)return LS_ClientAbort;
    ++formats;if(error==1)return LS_ClientAbort;
    dim->heightsRef=dim->heightsPres={20,10,30};dim->dur=error==2?-1:text.size()==3?50:70;
    *first=cp==0;*used=0;*before=cp==1&&text.size()==3?before_second:2;*after=cp==0&&text.size()==3?after_first:2;
    return 0;
}
static int WINAPI handler(void*,uint32_t id,InlineInit* value) {
    if(id!=1)return LS_InvalidObjectIdFetched;
    *value={0,reinterpret_cast<void*>(format_object),reinterpret_cast<void*>(draw_object)};return 0;
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"usage: check DLL");HMODULE dll=LoadLibraryA(argv[1]);require(dll!=nullptr,"load native library");
        using Create=int(WINAPI*)(LsContextInfo*,LscbkRedefined*,void**);
        using Doc=int(WINAPI*)(void*,int,int,LsDevRes*);
        using Line=int(WINAPI*)(void*,int,int,int,uint32_t,void*,LsLInfo*,void**,int*,LsLineWidths*);
        using Query=int(WINAPI*)(void*,int,int,LsQSubInfo*,int*,LsTextCell*);
        using Display=int(WINAPI*)(void*,LSPOINT*,uint32_t,LSRECT*);
        using Dispose=int(WINAPI*)(void*,BOOL);
        using Destroy=int(WINAPI*)(void*);
#define PROC(name,type) auto name=reinterpret_cast<type>(GetProcAddress(dll,#name));require(name!=nullptr,#name)
        PROC(LoCreateContext,Create);PROC(LoSetDoc,Doc);PROC(LoCreateLine,Line);PROC(LoQueryLineCpPpoint,Query);PROC(LoDisplayLine,Display);PROC(LoDisposeLine,Dispose);PROC(LoDestroyContext,Destroy);
        require(sizeof(InlineInit)==24&&offsetof(InlineInit,pfnFormat)==8&&offsetof(InlineInit,pfnDraw)==16&&sizeof(ObjDim)==28,"original object callback record ABI");
        LsContextInfo config{};config.version=4;config.pfnFetchPap=reinterpret_cast<void*>(pap);config.pfnGetRunTextMetrics=reinterpret_cast<void*>(metrics);config.pfnGetRunCharWidths=reinterpret_cast<void*>(widths);config.pfnDrawTextRun=reinterpret_cast<void*>(draw_text);config.pfnGetObjectHandlerInfo=reinterpret_cast<void*>(handler);
        LscbkRedefined callbacks{reinterpret_cast<void*>(fetch),nullptr,reinterpret_cast<void*>(props)};
        void* context=nullptr;LsDevRes resolution{1440,1440,1440,1440};
        require(LoCreateContext(&config,&callbacks,&context)==0&&LoSetDoc(context,1,1,&resolution)==0,"object context setup");
        LsLInfo info{};LsLineWidths measured{};void* line=nullptr;int depth=0;
        require(LoCreateLine(context,0,0,200,0,nullptr,&info,&line,&depth,&measured)==0&&formats==1&&measured.upLimLine==94&&info.dvpAscent==20&&info.dvpDescent==10&&info.dvpMultiLineHeight==30,"object format identity, pen, dimensions and line metrics");
        LsQSubInfo sub{};LsTextCell cell{};
        require(LoQueryLineCpPpoint(line,1,1,&sub,&depth,&cell)==0&&sub.idobj==1&&sub.plsrun==reinterpret_cast<void*>(6)&&cell.pointUvStartCell.x==14&&cell.dupCell==70&&cell.cGlyphsInCell==0,"object query retains identity and real atomic geometry without glyphs");
        LSPOINT origin{10,21};LSRECT clip{-100,-100,1000,1000};
        require(LoDisplayLine(line,&origin,1,&clip)==0&&draws==1&&object_origin.x==24&&object_origin.y==21,"original object draw ABI and baseline origin");
        error=3;require(LoDisplayLine(line,&origin,1,&clip)==LS_ClientAbort,"object draw failure propagates");error=0;
        require(LoDisposeLine(line,FALSE)==0,"object line disposal");
        for(int failure=1;failure<=2;++failure) {
            error=failure;line=reinterpret_cast<void*>(1);
            require(LoCreateLine(context,0,0,200,0,nullptr,&info,&line,&depth,&measured)==(failure==1?LS_ClientAbort:LS_InvalidFormatterResult)&&!line,"object format/invalid-dimension errors preserve empty output");
        }
        error=0;text=L"\xfffc\xfffc\x2029";
        // Original TextAndFontProperties.cs table, reordered to native
        // Never/Can/Please/Must rather than repeating the engine's predicate.
        const bool breaks[4][4]={{false,false,false,true},{false,false,true,true},{false,true,true,true},{true,true,true,true}};
        for(int left=0;left<4;++left)for(int right=0;right<4;++right) {
            after_first=left;before_second=right;
            bool allowed=breaks[left][right];
            require(LoCreateLine(context,0,0,80,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==(allowed?1:3)&&measured.upLimLine==(allowed?54:104),"all 16 original break-condition combinations match source table");
            require(LoDisposeLine(line,FALSE)==0,"break-table line disposal");
        }
        require(LoDestroyContext(context)==0,"object context teardown");
        std::puts("native InlineInit/ObjDim ABI, format/draw identity/geometry, atomic query, all 16 source break combinations, errors and ownership PASS");return 0;
    } catch(const std::exception& e) { std::printf("FAIL: %s\n",e.what());return 1; }
}
