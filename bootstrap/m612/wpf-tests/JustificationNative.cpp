// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include "LineServicesNative.h"
struct GlyphOffset { int32_t du,dv; };
static std::wstring text=L"aa bb cc dd\u2029";
static bool shaped=false,mixed=false,tab_mode=false;
static int map_failure=0,position_calls=0;
static int failure=0,calls=0,draws=0;
static bool compress_mode=false,compress_both=false;
static int compression_failure=0,compression_calls=0;
static void require(bool value,const char* why) { if(!value)throw std::runtime_error(why); }
static int WINAPI pap(void*,int,LsPap* value) { *value={};value->grpf=0x40;value->fJustify=1;value->lskj=mixed?4:0;return 0; }
static int WINAPI props(void*,int,int,LsLineProps* value) { *value={};value->durLeft=4;value->durRightJustify=3;return 0; }
static int WINAPI fetch(void*,int cp,int,void*,WCHAR* buffer,int,int* used,WCHAR**,int* count,int* hidden,LsChp* chp,uintptr_t* id) {
    if(cp<0 || cp>=static_cast<int>(text.size()))return LS_WrongGivenCp;
    int end=static_cast<int>(text.size())-1;*count=cp<end?end-cp:1;*used=1;*hidden=0;*chp={};chp->idObj=0xffff;chp->flags=shaped?0x400:0;*id=7;
    std::memcpy(buffer,text.data()+cp,*count*sizeof(WCHAR));return 0;
}
static int WINAPI metrics(void*,uint32_t,int,int,LsTxM* value) { *value={12,8,20,0};return 0; }
static int WINAPI widths(void*,uint32_t,int,WCHAR*,int count,int,int,int* values,int* total,int* fitted) { *total=count*10;*fitted=count;for(int i=0;i<count;++i)values[i]=10;return 0; }
static int WINAPI glyphs(void*,uintptr_t*,int*,int,WCHAR*,int count,int,uint16_t* indices,uint32_t* properties,int,int* used,uint16_t* clusters,uint16_t* chars,int* alone,int* actual) {
    *used=1;*actual=count;for(int i=0;i<count;++i){indices[i]=i+20;properties[i]=0;clusters[i]=i;chars[i]=0;alone[i]=1;}if(map_failure==1)clusters[0]=1;if(map_failure==2)clusters[count-1]=count;if(map_failure==3&&count>2)clusters[2]=0;return 0;
}
static int WINAPI positions(void*,uintptr_t*,int*,int,int,WCHAR*,uint16_t*,uint16_t*,int,uint16_t*,uint32_t*,int count,int,int* advances,GlyphOffset* offsets) {
    ++position_calls;for(int i=0;i<count;++i){advances[i]=10;offsets[i]={};}return 0;
}
static int fill(int count,int** left,int** right) {
    ++calls;if(failure==1)return LS_ClientAbort;
    for(int level=0;level<3;++level)for(int i=0;i<count;++i){left[level][i]=0;right[level][i]=failure==4?1:level==0?5:level==1?8:100;}
    if(failure==2)left[0][0]=-1;return 0;
}
static int WINAPI char_expand(void*,int device,int flow,LsCharRunInfo* run,LsNeighborInfo* left,LsNeighborInfo* right,int priorities,int** l,int** r) {
    if(tab_mode)return fill(run->cwch,l,r);
    if(device||flow||priorities!=3||run->plsrun!=7||run->cwch!=6||run->pwch[2]!=text[2]||run->rgduNominalWidth[2]!=10||run->rgduChangeLeft[2]||run->rgduChangeRight[2]||left->fNeighborIsPresent||right->fNeighborIsPresent)return LS_ClientAbort;
    int status=fill(run->cwch,l,r);if(mixed)for(int cp=0;cp<run->cwch;++cp)for(int level=0;level<2;++level)r[level][cp]=0;return status;
}
static int WINAPI glyph_expand(void*,int device,int flow,LsGlyphRunInfo* run,LsNeighborInfo*,LsNeighborInfo*,int priorities,int** l,int** r,uint8_t* types,int* ink) {
    if(tab_mode){for(int i=0;i<run->cgindex;++i){types[i]=1;ink[i]=0;}return fill(run->cgindex,l,r);}
    if(device||flow||priorities!=3||run->plsrun!=7||run->cwch!=6||run->cgindex!=6||run->pwch[2]!=text[2]||run->rggmap[2]!=2||run->rgduWidth[2]!=10||run->rggindex[2]!=22)return LS_ClientAbort;
    for(int i=0;i<run->cgindex;++i){types[i]=1;ink[i]=0;}if(failure==3)types[2]=2;
    int status=fill(run->cgindex,l,r);if(mixed)for(int cp=0;cp<run->cgindex;++cp)for(int level=0;level<2;++level)r[level][cp]=0;return status;
}
static int compression_fill(int count,int** left,int** right) {
    ++compression_calls;if(compression_failure==1)return LS_ClientAbort;
    for(int level=0;level<3;++level)for(int i=0;i<count;++i){int amount=compression_failure==3?1000:compression_failure==4?0:level==0?2:level==1?4:8;left[level][i]=compress_both?amount:0;right[level][i]=amount;}
    if(compression_failure==2)right[0][0]=-1;
    return 0;
}
static int WINAPI char_compress(void*,int device,int flow,LsCharRunInfo* run,LsNeighborInfo*,LsNeighborInfo*,int priorities,int** left,int** right) {
    if(device||flow||priorities!=3||run->plsrun!=7)return LS_ClientAbort;
    return compression_fill(run->cwch,left,right);
}
static int WINAPI glyph_compress(void*,int device,int flow,LsGlyphRunInfo* run,LsNeighborInfo*,LsNeighborInfo*,int priorities,int** left,int** right) {
    if(device||flow||priorities!=3||run->plsrun!=7||run->cwch!=run->cgindex)return LS_ClientAbort;
    return compression_fill(run->cgindex,left,right);
}
static int WINAPI draw_text(void*,uint32_t,LSPOINT*,WCHAR*,int* widths,int count,int,uint32_t,LSPOINT*,LsHeights*,int width,LSRECT*) {
    if(compress_mode){if(count!=9||widths[2]!=4||widths[5]!=4||widths[8]!=10||width!=78)return LS_ClientAbort;++draws;return 0;}
    if(count!=6||widths[2]!=23||widths[5]!=10||width!=73)return LS_ClientAbort;++draws;return 0;
}
static int WINAPI draw_glyphs(void*,uint32_t,WCHAR*,uint16_t*,uint16_t*,int count,uint16_t*,int* justified,int* nominal,GlyphOffset* offsets,uint32_t*,uint8_t* types,int,int,uint32_t,LSPOINT*,LsHeights*,int width,LSRECT*) {
    if(compress_mode){if(count!=9||justified[2]!=4||justified[5]!=4||justified[8]!=10||nominal[2]!=10||nominal[5]!=10||types[2]!=0||width!=78||offsets[2].du!=(compress_both?-3:0))return LS_ClientAbort;++draws;return 0;}
    if(count!=6||justified[2]!=23||nominal[2]!=10||types[2]!=1||width!=73)return LS_ClientAbort;++draws;return 0;
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"usage: check DLL");HMODULE dll=LoadLibraryA(argv[1]);require(dll!=nullptr,"native DLL load");
        using Create=int(WINAPI*)(LsContextInfo*,LscbkRedefined*,void**);using Doc=int(WINAPI*)(void*,int,int,LsDevRes*);
        using Line=int(WINAPI*)(void*,int,int,int,uint32_t,void*,LsLInfo*,void**,int*,LsLineWidths*);
        using SetTabs=int(WINAPI*)(void*,int,int,LsTbd*);
        using Query=int(WINAPI*)(void*,int,int,LsQSubInfo*,int*,LsTextCell*);using Display=int(WINAPI*)(void*,LSPOINT*,uint32_t,LSRECT*);
        using Dispose=int(WINAPI*)(void*,BOOL);using Destroy=int(WINAPI*)(void*);
#define PROC(name,type) auto name=reinterpret_cast<type>(GetProcAddress(dll,#name));require(name!=nullptr,#name)
        PROC(LoSetTabs,SetTabs);PROC(LoCreateContext,Create);PROC(LoSetDoc,Doc);PROC(LoCreateLine,Line);PROC(LoQueryLineCpPpoint,Query);PROC(LoDisplayLine,Display);PROC(LoDisposeLine,Dispose);PROC(LoDestroyContext,Destroy);
        require(sizeof(LsNeighborInfo)==28&&sizeof(LsCharRunInfo)==48&&sizeof(LsGlyphRunInfo)==80&&offsetof(LsGlyphRunInfo,rgduWidth)==56,"source nominal/glyph/neighbor record ABI");
        LsContextInfo config{};config.version=4;config.pfnFetchPap=reinterpret_cast<void*>(pap);config.pfnGetRunTextMetrics=reinterpret_cast<void*>(metrics);config.pfnGetRunCharWidths=reinterpret_cast<void*>(widths);config.pfnDrawTextRun=reinterpret_cast<void*>(draw_text);config.pfnDrawGlyphs=reinterpret_cast<void*>(draw_glyphs);config.pfnGetGlyphPositions=reinterpret_cast<void*>(positions);config.pfnGetCharExpansionInfoFullMixed=reinterpret_cast<void*>(char_expand);config.pfnGetGlyphExpansionInfoFullMixed=reinterpret_cast<void*>(glyph_expand);config.pfnGetCharCompressionInfoFullMixed=reinterpret_cast<void*>(char_compress);config.pfnGetGlyphCompressionInfoFullMixed=reinterpret_cast<void*>(glyph_compress);
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
        for(bool mode:{false,true}) {
            shaped=mode;text=L"abcdefgh ij\u2029";
            for(bool kind:{false,true}) {
                mixed=kind;calls=position_calls=0;
                require(LoCreateLine(context,0,0,70,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==6&&measured.upMinStartTrailing==64&&measured.upStartTrailing==(mixed?67:64),"mixed mode uses only emergency capacity while interword mode retains its ordinary no-space width");
                LsQSubInfo sub{};LsTextCell cell{};
                for(int cp=0;cp<6;++cp)require(LoQueryLineCpPpoint(line,cp,1,&sub,&depth,&cell)==0&&cell.dupCell==(mixed&&cp<3?11:10),"nominal and shaped emergency expansion preserves exact distributed caret advances");
                require(LoDisposeLine(line,FALSE)==0,"mixed emergency line disposal");
            }
        }
        mixed=false;shaped=true;text=L"aa bb cc dd\u2029";
        for(int malformed:{1,2,3}) {
            map_failure=malformed;position_calls=0;line=reinterpret_cast<void*>(1);
            require(LoCreateLine(context,0,0,70,0,nullptr,&info,&line,&depth,&measured)==LS_InvalidCharToGlyphMapping&&!line&&position_calls==0,"invalid first/range/decreasing cluster maps fail before glyph placement with empty output");
        }
        map_failure=0;
        std::puts("native mixed emergency expansion: nominal/glyph capacities, exact caret distribution, interword policy and invalid cluster isolation PASS");
        failure=3;line=reinterpret_cast<void*>(1);require(LoCreateLine(context,0,0,70,0,nullptr,&info,&line,&depth,&measured)==LS_NotImplemented&&!line,"unsupported ink expansion rejects without fabricated glyphs");
        failure=0;tab_mode=true;
        for(bool mode:{false,true})for(bool kind:{false,true})for(int earlier:{0,1,2,3,4}) {
            shaped=mode;mixed=kind;calls=0;
            text=L"a\tAB\t aa bb cc dd\u2029";
            LsTbd stops[3]{{earlier,60,0,L'.'},{0,90,0,0},{2,300,0,0}};
            require(LoSetTabs(context,80,3,stops)==0,"selected-stop setup");
            int status=LoCreateLine(context,0,0,160,0,nullptr,&info,&line,&depth,&measured);
            if(status)std::printf("selected tab status=%d shaped=%d mixed=%d earlier=%d\n",status,mode,kind,earlier);
            require(status==0&&info.cpLimToContinue==12&&measured.upStartTrailing==157&&measured.upMinStartTrailing==150,"earlier aligned field and unused right stop permit exact final left-field justification");
            LsQSubInfo sub{};LsTextCell cell{};
            require(LoQueryLineCpPpoint(line,2,1,&sub,&depth,&cell)==0&&cell.pointUvStartCell.x==(earlier==1?50:earlier?40:60)&&cell.dupCell==10,"earlier tab anchor remains nominal");
            require(LoQueryLineCpPpoint(line,5,1,&sub,&depth,&cell)==0&&cell.pointUvStartCell.x==90,"last left stop remains fixed");
            require(calls>0&&LoDisposeLine(line,FALSE)==0,"selected-stop expansion callback and disposal");
        }
        // The first stop is right-aligned; the next actual tab falls back to a
        // left incremental stop. Its remembered alignment must reset to left.
        text=L"a\tAB\t aa bb cc dd\u2029";mixed=false;
        LsTbd right_stop{2,60,0,0};require(LoSetTabs(context,80,1,&right_stop)==0,"incremental selected-stop setup");
        require(LoCreateLine(context,0,0,150,0,nullptr,&info,&line,&depth,&measured)==0&&measured.upStartTrailing==147,"incremental fallback after an aligned stop permits justification");
        require(LoDisposeLine(line,FALSE)==0,"incremental justified disposal");
        text=L"a\t aa bb cc dd ee ff gg hh ii jj kk ll mm nn\u2029";
        for(int alignment:{1,2,3,4}) {
            LsTbd final_stop{alignment,120,0,L'.'};
            require(LoSetTabs(context,80,1,&final_stop)==0,"final aligned field setup");
            line=reinterpret_cast<void*>(1);
            int status=LoCreateLine(context,0,0,165,0,nullptr,&info,&line,&depth,&measured);
            if(status!=LS_NotImplemented)std::printf("final aligned status=%d alignment=%d cp=%d end=%d width=%d minimum=%d\n",status,alignment,info.cpLimToContinue,info.endr,measured.upLimLine,measured.upMinLimLine);
            require(status==LS_NotImplemented&&!line,"final aligned field still rejects without publishing moved anchors");
        }
        std::puts("native selected tab justification: earlier anchors, unused stops, incremental fallback, nominal/shaped and mixed/interword PASS");
        tab_mode=false;compress_mode=true;text=L"aa bb cc dd\u2029";
        require(LoSetTabs(context,0,0,nullptr)==0,"clear tabs before compression");
        for(bool mode:{false,true})for(bool both:{false,true}) {
            shaped=mode;compress_both=both;mixed=true;compression_calls=draws=0;
            require(LoCreateLine(context,0,9,75,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==9&&measured.upStartTrailing==72&&measured.upLimLine==82&&measured.upMinStartTrailing==(both?64:68)&&measured.upMinLimLine==(both?74:78)&&compression_calls==1,"exact mixed compression, trailing spaces and full provider minima");
            LsQSubInfo sub{};LsTextCell cell{};
            require(LoQueryLineCpPpoint(line,2,1,&sub,&depth,&cell)==0&&cell.pointUvStartCell.x==24&&cell.dupCell==4,"compressed space retains actual caret geometry");
            require(LoQueryLineCpPpoint(line,6,1,&sub,&depth,&cell)==0&&cell.pointUvStartCell.x==52,"following text follows both compressed spaces");
            LSPOINT origin{0,12};LSRECT clip{-100,-100,1000,1000};require(LoDisplayLine(line,&origin,1,&clip)==0&&draws==1,"compressed drawing retains nominal glyphs and signed offsets");
            require(LoDisposeLine(line,FALSE)==0,"compressed line disposal");
            for(int bad:{1,2}){compression_failure=bad;line=reinterpret_cast<void*>(1);require(LoCreateLine(context,0,9,75,0,nullptr,&info,&line,&depth,&measured)==(bad==1?LS_ClientAbort:LS_InvalidFormatterResult)&&!line,"compression callback failure/negative capacity clears output");}
            compression_failure=3;require(LoCreateLine(context,0,9,47,0,nullptr,&info,&line,&depth,&measured)==0&&measured.upStartTrailing==64&&measured.upLimLine==74,"combined excessive capacities never create negative advances or compress nonspaces/trailing spaces");require(LoDisposeLine(line,FALSE)==0,"exhausted compression disposal");
            compression_failure=4;require(LoCreateLine(context,0,9,75,0,nullptr,&info,&line,&depth,&measured)==0&&measured.upStartTrailing==84,"zero compression capacity preserves nominal overflow");require(LoDisposeLine(line,FALSE)==0,"zero capacity disposal");compression_failure=0;
            mixed=false;compression_calls=0;require(LoCreateLine(context,0,9,75,0,nullptr,&info,&line,&depth,&measured)==0&&measured.upStartTrailing==84&&compression_calls==0,"InterWord retains ordinary wrapping policy");require(LoDisposeLine(line,FALSE)==0,"interword overflow disposal");
            mixed=true;require(LoCreateLine(context,0,12,110,0,nullptr,&info,&line,&depth,&measured)==0&&info.endr==2&&measured.upStartTrailing==107,"selected terminal optimal line can compress instead of overflowing");require(LoDisposeLine(line,FALSE)==0,"terminal compression disposal");
        }
        std::puts("native mixed compression: exact nominal/shaped/left-right geometry, priorities/minima, original drawing advances, capacity floors, terminal reconstruction and failure isolation PASS");
        require(LoDestroyContext(context)==0,"justification context teardown");
        std::puts("native nominal/glyph/neighbor ABI, exact justified geometry, original advances, trailing/minimum widths, priority limits, callback errors and final-line policy PASS");return 0;
    } catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}
}
