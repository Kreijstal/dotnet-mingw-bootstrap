// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include "LineServicesNative.h"
struct GlyphOffset { int32_t du,dv; };
static std::wstring text=L"aa bb cc dd\u2029";
static bool shaped=false;
static int failure=0,calls=0,draws=0;
static void require(bool value,const char* why) { if(!value)throw std::runtime_error(why); }
static int WINAPI pap(void*,int,LsPap* value) { *value={};value->grpf=0x40;value->fJustify=1;return 0; }
static int WINAPI props(void*,int,int,LsLineProps* value) { *value={};value->durLeft=4;value->durRightJustify=3;return 0; }
static int WINAPI fetch(void*,int cp,int,void*,WCHAR* buffer,int,int* used,WCHAR**,int* count,int* hidden,LsChp* chp,uintptr_t* id) {
    if(cp<0 || cp>=static_cast<int>(text.size()))return LS_WrongGivenCp;
    int end=static_cast<int>(text.size())-1;*count=cp<end?end-cp:1;*used=1;*hidden=0;*chp={};chp->idObj=0xffff;chp->flags=shaped?0x400:0;*id=7;
    std::memcpy(buffer,text.data()+cp,*count*sizeof(WCHAR));return 0;
}
static int WINAPI metrics(void*,uint32_t,int,int,LsTxM* value) { *value={12,8,20,0};return 0; }
static int WINAPI widths(void*,uint32_t,int,WCHAR*,int count,int,int,int* values,int* total,int* fitted) { *total=count*10;*fitted=count;for(int i=0;i<count;++i)values[i]=10;return 0; }
static int WINAPI glyphs(void*,uintptr_t*,int*,int,WCHAR*,int count,int,uint16_t* indices,uint32_t* properties,int,int* used,uint16_t* clusters,uint16_t* chars,int* alone,int* actual) {
    *used=1;*actual=count;for(int i=0;i<count;++i){indices[i]=i+20;properties[i]=0;clusters[i]=i;chars[i]=0;alone[i]=1;}return 0;
}
static int WINAPI positions(void*,uintptr_t*,int*,int,int,WCHAR*,uint16_t*,uint16_t*,int,uint16_t*,uint32_t*,int count,int,int* advances,GlyphOffset* offsets) {
    for(int i=0;i<count;++i){advances[i]=10;offsets[i]={};}return 0;
}
static int fill(int count,int** left,int** right) {
    ++calls;if(failure==1)return LS_ClientAbort;
    for(int level=0;level<3;++level)for(int i=0;i<count;++i){left[level][i]=0;right[level][i]=failure==4?1:level==0?5:level==1?8:100;}
    if(failure==2)left[0][0]=-1;return 0;
}
static int WINAPI char_expand(void*,int device,int flow,LsCharRunInfo* run,LsNeighborInfo* left,LsNeighborInfo* right,int priorities,int** l,int** r) {
    if(device||flow||priorities!=3||run->plsrun!=7||run->cwch!=6||run->pwch[2]!=' '||run->rgduNominalWidth[2]!=10||run->rgduChangeLeft[2]||run->rgduChangeRight[2]||left->fNeighborIsPresent||right->fNeighborIsPresent)return LS_ClientAbort;
    return fill(run->cwch,l,r);
}
static int WINAPI glyph_expand(void*,int device,int flow,LsGlyphRunInfo* run,LsNeighborInfo*,LsNeighborInfo*,int priorities,int** l,int** r,uint8_t* types,int* ink) {
    if(device||flow||priorities!=3||run->plsrun!=7||run->cwch!=6||run->cgindex!=6||run->pwch[2]!=' '||run->rggmap[2]!=2||run->rgduWidth[2]!=10||run->rggindex[2]!=22)return LS_ClientAbort;
    for(int i=0;i<run->cgindex;++i){types[i]=1;ink[i]=0;}if(failure==3)types[2]=2;
    return fill(run->cgindex,l,r);
}
static int WINAPI draw_text(void*,uint32_t,LSPOINT*,WCHAR*,int* widths,int count,int,uint32_t,LSPOINT*,LsHeights*,int width,LSRECT*) {
    if(count!=6||widths[2]!=23||widths[5]!=10||width!=73)return LS_ClientAbort;++draws;return 0;
}
static int WINAPI draw_glyphs(void*,uint32_t,WCHAR*,uint16_t*,uint16_t*,int count,uint16_t*,int* justified,int* nominal,GlyphOffset*,uint32_t*,uint8_t* types,int,int,uint32_t,LSPOINT*,LsHeights*,int width,LSRECT*) {
    if(count!=6||justified[2]!=23||nominal[2]!=10||types[2]!=1||width!=73)return LS_ClientAbort;++draws;return 0;
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"usage: check DLL");HMODULE dll=LoadLibraryA(argv[1]);require(dll!=nullptr,"native DLL load");
        using Create=int(WINAPI*)(LsContextInfo*,LscbkRedefined*,void**);using Doc=int(WINAPI*)(void*,int,int,LsDevRes*);
        using Line=int(WINAPI*)(void*,int,int,int,uint32_t,void*,LsLInfo*,void**,int*,LsLineWidths*);
        using Query=int(WINAPI*)(void*,int,int,LsQSubInfo*,int*,LsTextCell*);using Display=int(WINAPI*)(void*,LSPOINT*,uint32_t,LSRECT*);
        using Dispose=int(WINAPI*)(void*,BOOL);using Destroy=int(WINAPI*)(void*);
#define PROC(name,type) auto name=reinterpret_cast<type>(GetProcAddress(dll,#name));require(name!=nullptr,#name)
        PROC(LoCreateContext,Create);PROC(LoSetDoc,Doc);PROC(LoCreateLine,Line);PROC(LoQueryLineCpPpoint,Query);PROC(LoDisplayLine,Display);PROC(LoDisposeLine,Dispose);PROC(LoDestroyContext,Destroy);
        require(sizeof(LsNeighborInfo)==28&&sizeof(LsCharRunInfo)==48&&sizeof(LsGlyphRunInfo)==80&&offsetof(LsGlyphRunInfo,rgduWidth)==56,"source nominal/glyph/neighbor record ABI");
        LsContextInfo config{};config.version=4;config.pfnFetchPap=reinterpret_cast<void*>(pap);config.pfnGetRunTextMetrics=reinterpret_cast<void*>(metrics);config.pfnGetRunCharWidths=reinterpret_cast<void*>(widths);config.pfnDrawTextRun=reinterpret_cast<void*>(draw_text);config.pfnDrawGlyphs=reinterpret_cast<void*>(draw_glyphs);config.pfnGetGlyphPositions=reinterpret_cast<void*>(positions);config.pfnGetCharExpansionInfoFullMixed=reinterpret_cast<void*>(char_expand);config.pfnGetGlyphExpansionInfoFullMixed=reinterpret_cast<void*>(glyph_expand);
        LscbkRedefined callbacks{reinterpret_cast<void*>(fetch),reinterpret_cast<void*>(glyphs),reinterpret_cast<void*>(props)};
        void* context=nullptr;LsDevRes resolution{1440,1440,1440,1440};require(LoCreateContext(&config,&callbacks,&context)==0&&LoSetDoc(context,1,1,&resolution)==0,"justification context");
        LsLInfo info{};LsLineWidths measured{};void* line=nullptr;int depth=0;
        for(bool mode:{false,true}) {
            shaped=mode;calls=draws=0;
            require(LoCreateLine(context,0,0,70,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==6&&calls==1&&measured.upStartTrailing==67&&measured.upLimLine==77&&measured.upMinStartTrailing==54&&measured.upMinLimLine==64,"exact justified widths preserve nominal minima and trailing spaces");
            LsQSubInfo sub{};LsTextCell cell{};require(LoQueryLineCpPpoint(line,2,1,&sub,&depth,&cell)==0&&cell.pointUvStartCell.x==24&&cell.dupCell==23,"expanded space has retained actual caret geometry");
            require(LoQueryLineCpPpoint(line,3,1,&sub,&depth,&cell)==0&&cell.pointUvStartCell.x==47,"following text moves by exact distributed expansion");
            LSPOINT origin{0,12};LSRECT clip{-100,-100,1000,1000};require(LoDisplayLine(line,&origin,1,&clip)==0&&draws==1,"nominal/shaped drawing receives actual and original advances");
            require(LoDisposeLine(line,FALSE)==0,"justified line disposal");
            for(int bad=1;bad<=2;++bad){failure=bad;line=reinterpret_cast<void*>(1);require(LoCreateLine(context,0,0,70,0,nullptr,&info,&line,&depth,&measured)==(bad==1?LS_ClientAbort:LS_InvalidFormatterResult)&&!line,"callback failure/negative capacity preserves empty output");}
            failure=4;require(LoCreateLine(context,0,0,70,0,nullptr,&info,&line,&depth,&measured)==0&&measured.upStartTrailing==55,"provider capacity limits cannot be exceeded to forge column width");require(LoDisposeLine(line,FALSE)==0,"limited-capacity line disposal");failure=0;
            calls=0;require(LoCreateLine(context,0,0,200,0,nullptr,&info,&line,&depth,&measured)==0&&info.endr==2&&calls==0&&measured.upLimLine==114,"last paragraph line stays nominal and does not request expansion");require(LoDisposeLine(line,FALSE)==0,"last line disposal");
        }
        failure=3;line=reinterpret_cast<void*>(1);require(LoCreateLine(context,0,0,70,0,nullptr,&info,&line,&depth,&measured)==LS_NotImplemented&&!line,"unsupported ink expansion rejects without fabricated glyphs");
        require(LoDestroyContext(context)==0,"justification context teardown");
        std::puts("native nominal/glyph/neighbor ABI, exact justified geometry, original advances, trailing/minimum widths, priority limits, callback errors and final-line policy PASS");return 0;
    } catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}
}
