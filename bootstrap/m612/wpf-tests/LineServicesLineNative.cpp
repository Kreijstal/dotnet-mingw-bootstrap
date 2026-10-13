// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <vector>
#include "LineServicesNative.h"
static bool abort_fetch=false;
static int draws=0;
static std::wstring sample=L"abc  \u2029";
static uint32_t paragraph_flags=0;
static int run_length=9600;
static bool uniform_width=false;
static bool contextual_widths=false;
static bool decorate=false;
static int decoration_error=0;
struct DecorationCall { uint32_t kind; int x,y,width,thickness; };
static std::vector<DecorationCall> decorations;
struct TabCall { int cp,x,width; uint16_t leader; };
static std::vector<TabCall> tab_calls;
static int leader_draws=0;
static bool tab_abort=false;
static int WINAPI tab_draw(void*,uint32_t id,LSPOINT*,WCHAR* text,int* values,int count,int flow,uint32_t mode,LSPOINT*,LsHeights*,int width,LSRECT*) {
    if(id!=5 || flow!=0 || mode!=1) return LS_ClientAbort;
    if(text[0]==L'.' && count>1) {
        int sum=0; for(int i=0;i<count;++i) { if(text[i]!=L'.' || values[i]!=5) return LS_ClientAbort; sum+=values[i]; }
        if(sum!=width) return LS_ClientAbort;
        ++leader_draws;
        if(tab_abort) return LS_ClientAbort;
    }
    return 0;
}
static int WINAPI enum_tab(void*,uint32_t id,int cp,WCHAR* text,uint16_t leader,int flow,int reverse,int geometry,LSPOINT* point,LsHeights* height,int width) {
    if(id!=5 || text[0]!=L'\t' || flow!=0 || reverse || !geometry || height->dvAscent!=12) return LS_ClientAbort;
    tab_calls.push_back({cp,point->x,width,leader}); return tab_abort?LS_ClientAbort:0;
}
static int WINAPI enum_text(void*,uint32_t,int,int,WCHAR*,int,int,int,int,LSPOINT*,LsHeights*,int,int,int*,uint16_t*,uint16_t*,uint16_t*,int*,void*,uint32_t*,int) { return 0; }
static int WINAPI pap(void*,int,LsPap* value) { *value={}; value->cpFirst=0; value->grpf=paragraph_flags; return 0; }
static int WINAPI props(void*,int,int,LsLineProps* value) { *value={}; value->durLeft=4; return 0; }
static int WINAPI fetch(void*,int cp,int,void*,WCHAR* buffer,int capacity,int* used,WCHAR** output,int* count,int* hidden,LsChp* chp,uintptr_t* id) {
    if(abort_fetch) return LS_ClientAbort;
    int end=static_cast<int>(sample.size())-1;
    if(cp<0 || cp>end) return LS_WrongGivenCp;
    *count=cp<end?std::min(run_length,end-cp):1; *output=nullptr; *used=0; *hidden=0; *chp={}; chp->idObj=0xFFFF; *id=cp<end?5:8;
    if(decorate && cp<end) chp->flags=0x60000;
    if(capacity>=*count) { std::memcpy(buffer,sample.data()+cp,*count*sizeof(WCHAR)); *used=1; }
    return 0;
}
static int WINAPI metrics(void*,uint32_t,int,int,LsTxM* value) { *value={12,8,20,0}; return 0; }
static int WINAPI widths(void*,uint32_t,int,WCHAR* text,int count,int,int,int* values,int* total,int* fitted) {
    *total=0; *fitted=count;
    for(int i=0;i<count;++i) { values[i]=contextual_widths&&count<6?15:uniform_width?10:text[i]==L'a'?10:text[i]==L'b'?20:text[i]==L'c'?30:5; *total+=values[i]; }
    return 0;
}
static int WINAPI draw(void*,uint32_t,LSPOINT* point,WCHAR* text,int* values,int count,int,uint32_t,LSPOINT*,LsHeights* height,int width,LSRECT*) {
    if(point->x!=4 || point->y!=12 || count!=5 || text[0]!=L'a' || values[2]!=30 || width!=70 || height->dvAscent!=12) return LS_ClientAbort;
    ++draws; return 0;
}
static void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
static int WINAPI underline_info(void*,uint32_t id,LsHeights* height,int flow,LsULInfo* value) {
    if(id!=5 || height->dvAscent!=12 || height->dvDescent!=8 || flow!=0) return LS_ClientAbort;
    if(decoration_error==1) return LS_ClientAbort;
    *value={7,decoration_error==2?3:2,1,3,2,1,4}; return 0;
}
static int WINAPI strike_info(void*,uint32_t id,LsHeights*,int,LsStInfo* value) {
    if(id!=5) return LS_ClientAbort;
    *value={9,2,5,3,8,1}; return 0;
}
static int WINAPI decoration_draw(void*,uint32_t id,uint32_t kind,LSPOINT* point,int width,int thickness,int flow,uint32_t mode,LSRECT* clip) {
    if(id!=5 || flow!=0 || mode!=1 || clip->left!=-100) return LS_ClientAbort;
    if(decoration_error==3) return LS_ClientAbort;
    decorations.push_back({kind,point->x,point->y,width,thickness}); return 0;
}
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
        using CpQuery=int (WINAPI *)(void*,int,int,LsQSubInfo*,int*,LsTextCell*);
        using PointQuery=int (WINAPI *)(void*,const LSPOINT*,int,LsQSubInfo*,int*,LsTextCell*);
        using SetTabs=int (WINAPI *)(void*,int,int,const LsTbd*);
        using EnumLine=int (WINAPI *)(void*,BOOL,BOOL,LSPOINT*);
#define PROC(name,type) auto name=reinterpret_cast<type>(GetProcAddress(dll,#name)); require(name!=nullptr,#name)
        PROC(LoCreateContext,CreateContext); PROC(LoDestroyContext,DestroyContext); PROC(LoSetDoc,Doc);
        PROC(LoCreateLine,CreateLine); PROC(LoDisposeLine,DisposeLine); PROC(LoDisplayLine,Display);
        PROC(LoAcquireBreakRecord,Acquire); PROC(LoCloneBreakRecord,Acquire); PROC(LoDisposeBreakRecord,DisposeLine);
        PROC(LoQueryLineCpPpoint,CpQuery); PROC(LoQueryLinePointPcp,PointQuery);
        PROC(LoSetTabs,SetTabs); PROC(LoEnumLine,EnumLine);
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
        LsQSubInfo sub{}; LsTextCell cell{}; int queried_depth=0;
        require(sizeof(LsQSubInfo)==120&&sizeof(LsTextCell)==40,"original nested query ABI layouts");
        require(LoQueryLineCpPpoint(line,2,1,&sub,&queried_depth,&cell)==0&&queried_depth==1&&cell.lscpStartCell==2&&cell.lscpEndCell==2&&cell.pointUvStartCell.x==34&&cell.dupCell==30,"character position yields exact measured cell");
        require(sub.plsrun==reinterpret_cast<void*>(5)&&sub.pointUvStartRun.x==4&&sub.dupRun==70&&sub.lsdcpSubLine==6,"query retains original run identity and geometry");
        LSPOINT query{24,100};
        require(LoQueryLinePointPcp(line,&query,1,&sub,&queried_depth,&cell)==0&&cell.lscpStartCell==1&&cell.pointUvStartCell.x==14&&cell.dupCell==20,"point yields exact measured cell independent of vertical coordinate");
        query.x=-100;
        require(LoQueryLinePointPcp(line,&query,1,&sub,&queried_depth,&cell)==0&&cell.lscpStartCell==0,"point before text clamps to first cell");
        query.x=10000;
        require(LoQueryLinePointPcp(line,&query,1,&sub,&queried_depth,&cell)==0&&cell.lscpStartCell==4,"point after text clamps to last visible cell");
        require(LoQueryLineCpPpoint(line,5,1,&sub,&queried_depth,&cell)==0&&cell.dupCell==0&&cell.pointUvStartCell.x==74&&sub.plsrun==reinterpret_cast<void*>(8),"paragraph separator has actual zero-width position and run identity");
        require(LoQueryLineCpPpoint(line,6,1,&sub,&queried_depth,&cell)==LS_WrongGivenCp&&queried_depth==0&&cell.cCharsInCell==0,"out-of-range CP fails with empty output");
        require(LoQueryLineCpPpoint(line,0,0,&sub,&queried_depth,&cell)==LS_InvalidParameter,"insufficient query depth rejected");
        LSPOINT origin{0,12}; LSRECT clip{-100,-100,1000,1000};
        require(LoDisplayLine(line,&origin,1,&clip)==0&&draws==1,"retained text/widths/metrics drawing callback");
        require(LoDisposeLine(line,FALSE)==0&&LoDisposeLine(line,FALSE)==LS_InvalidLine,"owned line disposal");
        config.pfnGetRunUnderlineInfo=reinterpret_cast<void*>(underline_info);
        config.pfnGetRunStrikethroughInfo=reinterpret_cast<void*>(strike_info);
        config.pfnDrawUnderline=config.pfnDrawStrikethrough=reinterpret_cast<void*>(decoration_draw);
        void* decorated_context=nullptr;
        require(LoCreateContext(&config,&callbacks,&decorated_context)==0,"decoration context creation");
        require(LoSetDoc(decorated_context,1,1,&resolution)==0,"decoration context device");
        decorate=true;
        require(LoCreateLine(decorated_context,0,0,1000,0,nullptr,&info,&line,&depth,&measured)==0,"decorated line creation");
        require(LoDisplayLine(line,&origin,1,&clip)==0&&decorations.size()==4,"original decoration callback ABI");
        const int ys[]={16,19,7,4},thicknesses[]={2,4,3,1};
        for(size_t i=0;i<decorations.size();++i) require(decorations[i].kind==(i<2?7u:9u)&&decorations[i].x==4&&decorations[i].y==ys[i]&&decorations[i].width==70&&decorations[i].thickness==thicknesses[i],"decoration baseline offset/width/thickness/identity");
        decoration_error=1;require(LoDisplayLine(line,&origin,1,&clip)==LS_ClientAbort,"decoration metric failure propagation");
        decoration_error=2;require(LoDisplayLine(line,&origin,1,&clip)==LS_InvalidFormatterResult,"invalid decoration metrics rejected");
        decoration_error=3;require(LoDisplayLine(line,&origin,1,&clip)==LS_ClientAbort,"decoration draw failure propagation");
        decoration_error=0;decorate=false;
        require(LoDisposeLine(line,FALSE)==0&&LoDestroyContext(decorated_context)==0,"decoration line/context teardown");
        std::puts("native decoration callback ABI, single/double offsets, widths, thicknesses, errors and ownership PASS");
        require(LoQueryLineCpPpoint(line,0,1,&sub,&queried_depth,&cell)==LS_InvalidLine&&queried_depth==0,"disposed line query fails without dereferencing handle");
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
        sample=L"abcdef\u2029";run_length=9600;contextual_widths=true;
        require(LoCreateLine(context,0,0,34,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==2&&measured.upLimLine==34,"changed retained-prefix advances retry an earlier fitting candidate");
        require(LoDisposeLine(line,FALSE)==0,"remeasured prefix line disposal");contextual_widths=false;
        sample=L"a\u0301b\u2029"; run_length=1;
        require(LoCreateLine(context,0,0,5,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==2,"combining grapheme spans client runs at tiny width");
        require(LoQueryLineCpPpoint(line,0,1,&sub,&queried_depth,&cell)==LS_NotImplemented&&queried_depth==0,"cross-run caret ownership remains explicit rather than splitting a grapheme");
        require(LoDisposeLine(line,FALSE)==0,"grapheme line disposal");
        sample=L"a  \u2029";
        require(LoCreateLine(context,0,0,14,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==4&&info.endr==2&&measured.upStartTrailing==14,"trailing spaces across runs retain paragraph end");
        require(LoDisposeLine(line,FALSE)==0,"trailing line disposal");
        // Exercise tab geometry independently of managed callback behavior.
        paragraph_flags=0; uniform_width=false; run_length=1; sample=L"a\tb.c\u2029";
        config.pfnDrawTextRun=reinterpret_cast<void*>(tab_draw);
        config.pfnEnumTab=reinterpret_cast<void*>(enum_tab);
        config.pfnEnumText=reinterpret_cast<void*>(enum_text);
        void* tab_context=nullptr;
        require(LoCreateContext(&config,&callbacks,&tab_context)==0&&LoSetDoc(tab_context,1,1,&resolution)==0,"tab context setup");
        const int starts[]={100,73,45,80,80},ends[]={155,128,100,135,135};
        for(int kind=0;kind<5;++kind) {
            LsTbd stop{kind,100,L'.',L'.'};
            require(LoSetTabs(tab_context,40,1,&stop)==0,"tab stop ABI");
            require(LoCreateLine(tab_context,0,0,1000,0,nullptr,&info,&line,&depth,&measured)==0&&measured.upLimLine==ends[kind],"aligned field measured across client runs");
            require(LoQueryLineCpPpoint(line,1,1,&sub,&queried_depth,&cell)==0&&cell.pointUvStartCell.x==14&&cell.dupCell==starts[kind]-14,"tab caret spans actual gap");
            require(LoQueryLineCpPpoint(line,2,1,&sub,&queried_depth,&cell)==0&&cell.pointUvStartCell.x==starts[kind],"following field starts at selected alignment");
            tab_calls.clear();leader_draws=0;
            require(LoEnumLine(line,FALSE,TRUE,&origin)==0&&tab_calls.size()==1&&tab_calls[0].cp==1&&tab_calls[0].x==14&&tab_calls[0].width==starts[kind]-14&&tab_calls[0].leader==L'.',"original EnumTab ABI retains CP/leader/geometry");
            require(LoDisplayLine(line,&origin,1,&clip)==0&&leader_draws==1,"leader glyphs measured and drawn through original callbacks");
            tab_abort=true;
            require(LoDisplayLine(line,&origin,1,&clip)==LS_ClientAbort&&LoEnumLine(line,FALSE,TRUE,&origin)==LS_ClientAbort,"tab callback errors propagate");
            tab_abort=false;
            require(LoDisposeLine(line,FALSE)==0,"tab line disposal");
        }
        sample=L"a\tb\tc\u2029";
        require(LoSetTabs(tab_context,40,0,nullptr)==0,"incremental tab setup");
        require(LoCreateLine(tab_context,0,0,1000,0,nullptr,&info,&line,&depth,&measured)==0&&measured.upLimLine==110,"incremental tabs advance to successive stops");
        require(LoDisposeLine(line,FALSE)==0,"incremental tab line disposal");
        sample=L"a\tb.c\u2029";paragraph_flags=0x40;
        LsTbd right_stop{2,100,0,0};
        require(LoSetTabs(tab_context,40,1,&right_stop)==0,"aligned wrap tab setup");line=reinterpret_cast<void*>(1);
        require(LoCreateLine(tab_context,0,0,80,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==1&&measured.upLimLine==14,"out-of-column aligned stop breaks before tab");
        require(LoDisposeLine(line,FALSE)==0,"before-tab line disposal");
        require(LoCreateLine(tab_context,1,0,80,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==2&&measured.upLimLine==100,"indivisible first tab outside column advances with explicit overflow");
        require(LoDisposeLine(line,FALSE)==0,"overflow tab line disposal");
        uniform_width=true;sample=L"a\tabc def\u2029";
        right_stop.ur=60;
        require(LoSetTabs(tab_context,40,1,&right_stop)==0,"right aligned retained field setup");
        require(LoCreateLine(tab_context,0,0,70,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==6&&measured.upLimLine==60&&measured.upStartTrailing==50,"right aligned wrap chooses fitting retained field rather than full paragraph");
        require(LoQueryLineCpPpoint(line,1,1,&sub,&queried_depth,&cell)==0&&cell.pointUvStartCell.x==14&&cell.dupCell==6,"wrapped tab retains recomputed gap geometry");
        require(LoAcquireBreakRecord(line,&record)==0&&LoCloneBreakRecord(record,&clone)==0,"aligned wrap continuation owns cloned break record");
        require(LoDisposeLine(line,FALSE)==0&&LoDisposeBreakRecord(record,FALSE)==0,"aligned wrap original line/record disposal");
        require(LoCreateLine(tab_context,6,0,70,0,clone,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==10&&measured.upLimLine==34,"aligned wrap clone resumes complete remaining field");
        require(LoDisposeLine(line,FALSE)==0&&LoDisposeBreakRecord(clone,FALSE)==0,"aligned wrap continuation disposal");
        uniform_width=false;
        paragraph_flags=0;
        require(LoSetTabs(tab_context,0,0,nullptr)==0,"zero incremental tab setup");line=reinterpret_cast<void*>(1);
        require(LoCreateLine(tab_context,0,0,1000,0,nullptr,&info,&line,&depth,&measured)==LS_InvalidFormatterResult&&!line,"missing tab stops reject without fabricated geometry");
        require(LoDestroyContext(tab_context)==0,"tab context disposal");
        std::puts("native tab stop/EnumTab ABI, five alignments, cross-run fields, leader drawing, caret geometry, incremental stops and errors PASS");
        require(LoDestroyContext(context)==0,"line context disposal");
        std::puts("native line callbacks, exact metrics/CP-point geometry, query ABI/identity/failure isolation, cross-run word/grapheme wrapping, overflow/emergency policies, trailing spaces and native break-record clone/continuation/ownership PASS");
        return 0;
    } catch(const std::exception& failure) { std::printf("FAIL: %s\n",failure.what()); return 1; }
}
