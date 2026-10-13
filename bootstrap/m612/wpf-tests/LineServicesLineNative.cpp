// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "LineServicesNative.h"
static bool abort_fetch=false;
static int draws=0;
static int WINAPI pap(void*,int cp,LsPap* value) { *value={}; value->cpFirst=cp; return 0; }
static int WINAPI props(void*,int,int,LsLineProps* value) { *value={}; value->durLeft=4; return 0; }
static int WINAPI fetch(void*,int cp,int,void*,WCHAR* buffer,int capacity,int* used,WCHAR** output,int* count,int* hidden,LsChp* chp,uintptr_t* id) {
    if(abort_fetch) return LS_ClientAbort;
    static const WCHAR text[]=L"abc  \u2029";
    if(cp<0 || cp>5) return LS_WrongGivenCp;
    *count=cp<5?5-cp:1; *output=nullptr; *used=0; *hidden=0; *chp={}; chp->idObj=0xFFFF; *id=cp<5?5:8;
    if(capacity>=*count) { std::memcpy(buffer,text+cp,*count*sizeof(WCHAR)); *used=1; }
    return 0;
}
static int WINAPI metrics(void*,uint32_t,int,int,LsTxM* value) { *value={12,8,20,0}; return 0; }
static int WINAPI widths(void*,uint32_t,int,WCHAR* text,int count,int,int,int* values,int* total,int* fitted) {
    *total=0; *fitted=count;
    for(int i=0;i<count;++i) { values[i]=text[i]==L'a'?10:text[i]==L'b'?20:text[i]==L'c'?30:5; *total+=values[i]; }
    return 0;
}
static int WINAPI draw(void*,uint32_t,LSPOINT* point,WCHAR* text,int* values,int count,int,uint32_t,LSPOINT*,LsHeights* height,int width,LSRECT*) {
    if(point->x!=4 || point->y!=12 || count!=5 || text[0]!=L'a' || values[2]!=30 || width!=70 || height->dvAscent!=12) return LS_ClientAbort;
    ++draws; return 0;
}
static void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main(int argc,char** argv) {
    try {
        require(argc==2,"usage: check DLL");
        HMODULE dll=LoadLibraryA(argv[1]); require(dll!=nullptr,"native DLL load");
        using CreateContext=int (WINAPI *)(LsContextInfo*,LscbkRedefined*,void**);
        using DestroyContext=int (WINAPI *)(void*);
        using Doc=int (WINAPI *)(void*,int,int,const LsDevRes*);
        using CreateLine=int (WINAPI *)(void*,int,int,int,uint32_t,void*,LsLInfo*,void**,int*,LsLineWidths*);
        using DisposeLine=int (WINAPI *)(void*,BOOL);
        using Display=int (WINAPI *)(void*,LSPOINT*,uint32_t,LSRECT*);
#define PROC(name,type) auto name=reinterpret_cast<type>(GetProcAddress(dll,#name)); require(name!=nullptr,#name)
        PROC(LoCreateContext,CreateContext); PROC(LoDestroyContext,DestroyContext); PROC(LoSetDoc,Doc);
        PROC(LoCreateLine,CreateLine); PROC(LoDisposeLine,DisposeLine); PROC(LoDisplayLine,Display);
        LsContextInfo config{}; config.version=4; config.pfnFetchPap=reinterpret_cast<void*>(pap);
        config.pfnGetRunTextMetrics=reinterpret_cast<void*>(metrics); config.pfnGetRunCharWidths=reinterpret_cast<void*>(widths);
        config.pfnDrawTextRun=reinterpret_cast<void*>(draw);
        LscbkRedefined callbacks{reinterpret_cast<void*>(fetch),nullptr,reinterpret_cast<void*>(props)};
        void* context=nullptr; require(LoCreateContext(&config,&callbacks,&context)==0,"line context creation");
        LsDevRes resolution{1440,1440,1440,1440}; require(LoSetDoc(context,1,1,&resolution)==0,"line device setup");
        LsLInfo info{}; LsLineWidths measured{}; int depth=0; void* line=nullptr;
        require(LoCreateLine(context,0,0,1000,0,nullptr,&info,&line,&depth,&measured)==0&&line&&depth==1,"measured line creation");
        require(info.cpLimToContinue==6&&info.endr==2&&info.dvrAscent==12&&info.dvrDescent==8&&info.dvrMultiLineHeight==20,"callback-derived line metrics and paragraph end");
        require(measured.upStartMainText==4&&measured.upLimLine==74&&measured.upStartTrailing==64,"callback-derived width and trailing spaces");
        LSPOINT origin{0,12}; LSRECT clip{-100,-100,1000,1000};
        require(LoDisplayLine(line,&origin,1,&clip)==0&&draws==1,"retained text/widths/metrics drawing callback");
        require(LoDisposeLine(line,FALSE)==0&&LoDisposeLine(line,FALSE)==LS_InvalidLine,"owned line disposal");
        abort_fetch=true; line=reinterpret_cast<void*>(1);
        require(LoCreateLine(context,0,0,1000,0,nullptr,&info,&line,&depth,&measured)==LS_ClientAbort&&!line,"callback abort propagated without fabricated line");
        require(LoDestroyContext(context)==0,"line context disposal");
        std::puts("native line callbacks, paragraph boundary, exact metrics/widths/trailing spaces, retained drawing, abort and ownership PASS");
        return 0;
    } catch(const std::exception& failure) { std::printf("FAIL: %s\n",failure.what()); return 1; }
}
