// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <algorithm>
#include "LineServicesNative.h"
static bool abort_fetch=false;
static int draws=0;
static std::wstring sample=L"abc  \u2029";
static uint32_t paragraph_flags=0;
static int run_length=9600;
static bool uniform_width=false;
static int WINAPI pap(void*,int,LsPap* value) { *value={}; value->cpFirst=0; value->grpf=paragraph_flags; return 0; }
static int WINAPI props(void*,int,int,LsLineProps* value) { *value={}; value->durLeft=4; return 0; }
static int WINAPI fetch(void*,int cp,int,void*,WCHAR* buffer,int capacity,int* used,WCHAR** output,int* count,int* hidden,LsChp* chp,uintptr_t* id) {
    if(abort_fetch) return LS_ClientAbort;
    int end=static_cast<int>(sample.size())-1;
    if(cp<0 || cp>end) return LS_WrongGivenCp;
    *count=cp<end?std::min(run_length,end-cp):1; *output=nullptr; *used=0; *hidden=0; *chp={}; chp->idObj=0xFFFF; *id=cp<end?5:8;
    if(capacity>=*count) { std::memcpy(buffer,sample.data()+cp,*count*sizeof(WCHAR)); *used=1; }
    return 0;
}
static int WINAPI metrics(void*,uint32_t,int,int,LsTxM* value) { *value={12,8,20,0}; return 0; }
static int WINAPI widths(void*,uint32_t,int,WCHAR* text,int count,int,int,int* values,int* total,int* fitted) {
    *total=0; *fitted=count;
    for(int i=0;i<count;++i) { values[i]=uniform_width?10:text[i]==L'a'?10:text[i]==L'b'?20:text[i]==L'c'?30:5; *total+=values[i]; }
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
        using Acquire=int (WINAPI *)(void*,void**);
#define PROC(name,type) auto name=reinterpret_cast<type>(GetProcAddress(dll,#name)); require(name!=nullptr,#name)
        PROC(LoCreateContext,CreateContext); PROC(LoDestroyContext,DestroyContext); PROC(LoSetDoc,Doc);
        PROC(LoCreateLine,CreateLine); PROC(LoDisposeLine,DisposeLine); PROC(LoDisplayLine,Display);
        PROC(LoAcquireBreakRecord,Acquire); PROC(LoCloneBreakRecord,Acquire); PROC(LoDisposeBreakRecord,DisposeLine);
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
        abort_fetch=false; uniform_width=true; paragraph_flags=0x40;
        sample=L"abc def ghi\u2029"; run_length=2;
        require(LoCreateLine(context,0,0,34,0,nullptr,&info,&line,&depth,&measured)==0,"wrap through split client runs");
        require(info.cpLimToContinue==4&&info.endr==0&&info.fFirstLineInPara&&measured.upStartTrailing==34&&measured.upLimLine==44,"word boundary includes trailing space without exceeding visible column");
        void* record=nullptr; void* clone=nullptr;
        require(LoAcquireBreakRecord(line,&record)==0&&record,"acquire real native break record");
        require(LoCloneBreakRecord(record,&clone)==0&&clone&&clone!=record,"independent cloned record");
        require(LoDisposeLine(line,FALSE)==0&&LoDisposeBreakRecord(record,FALSE)==0,"record survives line and original-record disposal");
        require(LoCreateLine(context,5,0,34,0,clone,&info,&line,&depth,&measured)==LS_InvalidBreakRecord&&!line,"record rejects wrong continuation position");
        void* other=nullptr;
        require(LoCreateContext(&config,&callbacks,&other)==0&&LoSetDoc(other,1,1,&resolution)==0,"second context");
        require(LoCreateLine(other,4,0,34,0,clone,&info,&line,&depth,&measured)==LS_InvalidBreakRecord&&!line,"record rejects foreign context");
        require(LoDestroyContext(other)==0,"second context disposal");
        require(LoCreateLine(context,4,0,34,0,clone,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==8&&!info.fFirstLineInPara,"cloned record resumes paragraph");
        require(LoDisposeLine(line,FALSE)==0&&LoDisposeBreakRecord(clone,FALSE)==0&&LoDisposeBreakRecord(clone,FALSE)==LS_InvalidBreakRecord,"clone disposal rejects double release");
        void* invalid=reinterpret_cast<void*>(1);
        require(LoCloneBreakRecord(nullptr,&invalid)==LS_InvalidBreakRecord&&!invalid,"invalid clone output cleared");
        sample=L"abcdefgh ij\u2029"; paragraph_flags=0x40|0x80000;
        require(LoCreateLine(context,0,0,34,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==9,"WrapWithOverflow keeps a word across client runs");
        require(LoDisposeLine(line,FALSE)==0,"overflow line disposal");
        paragraph_flags=0x40;
        require(LoCreateLine(context,0,0,34,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==3,"emergency wrap advances complete graphemes");
        require(LoDisposeLine(line,FALSE)==0,"emergency line disposal");
        sample=L"a\u0301b\u2029"; run_length=1;
        require(LoCreateLine(context,0,0,5,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==2,"combining grapheme spans client runs at tiny width");
        require(LoDisposeLine(line,FALSE)==0,"grapheme line disposal");
        sample=L"a  \u2029";
        require(LoCreateLine(context,0,0,14,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==4&&info.endr==2&&measured.upStartTrailing==14,"trailing spaces across runs retain paragraph end");
        require(LoDisposeLine(line,FALSE)==0,"trailing line disposal");
        require(LoDestroyContext(context)==0,"line context disposal");
        std::puts("native line callbacks, exact metrics, cross-run word/grapheme wrapping, overflow/emergency policies, trailing spaces, native break-record clone/continuation/ownership and failure isolation PASS");
        return 0;
    } catch(const std::exception& failure) { std::printf("FAIL: %s\n",failure.what()); return 1; }
}
