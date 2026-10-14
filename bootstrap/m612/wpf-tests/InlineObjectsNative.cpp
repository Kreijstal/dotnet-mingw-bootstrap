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
static std::vector<int> object_positions;
static bool flexible=false;static int last_position=0,flexible_failure=0,object_column=800;
static bool justify=false,mixed=false;
static void require(bool value,const char* why) { if(!value)throw std::runtime_error(why); }
static int WINAPI pap(void*,int,LsPap* value) { *value={};value->grpf=0x40;value->fJustify=justify?1:0;value->lskj=mixed?4:0;return 0; }
static int WINAPI props(void*,int,int,LsLineProps* value) { *value={};value->durLeft=4;return 0; }
static int WINAPI fetch(void*,int cp,int,void*,WCHAR* buffer,int,int* used,WCHAR**,int* count,int* hidden,LsChp* chp,uintptr_t* id) {
    if(cp<0 || cp>=static_cast<int>(text.size()))return LS_WrongGivenCp;
    *used=1;*count=1;*hidden=0;buffer[0]=text[cp];*chp={};chp->idObj=text[cp]==0xfffc?1:0xffff;*id=cp+5;return 0;
}
static int WINAPI metrics(void*,uint32_t,int,int,LsTxM* value) { *value={12,8,20,0};return 0; }
static int WINAPI widths(void*,uint32_t,int,WCHAR*,int count,int,int,int* values,int* total,int* fitted) { *total=10*count;*fitted=count;for(int i=0;i<count;++i)values[i]=10;return 0; }
static int WINAPI draw_text(void*,uint32_t,LSPOINT*,WCHAR*,int*,int,int,uint32_t,LSPOINT*,LsHeights*,int,LSRECT*) { return 0; }
static int WINAPI draw_object(void*,uint32_t id,LSPOINT* point,int flow,int width) {
    int pen=id>=5&&id-5<object_positions.size()?object_positions[id-5]:last_position;
    if(flow || width!=(flexible?(object_column-pen)/2:text.size()==3?50:70) || id<5)return LS_ClientAbort;
    object_origin=*point;++draws;return error==3?LS_ClientAbort:0;
}
static int WINAPI format_object(void*,uint32_t id,int cp,int position,int margin,ObjDim* dim,int* first,int* used,int* before,int* after) {
    if(id!=static_cast<uint32_t>(cp+5) || margin!=LS_IdealInfiniteWidth || position<4)return LS_ClientAbort;
    ++formats;if(error==1||(flexible_failure==1&&formats==2)||(flexible_failure==4&&position!=44))return LS_ClientAbort;
    if(flexible) {
        last_position=position;if(object_positions.size()<text.size())object_positions.resize(text.size());object_positions[cp]=position;
        int width=flexible_failure==2?(position>=300?400:100):flexible_failure==3?(position>=100?150:50):(object_column-position)/2;
        dim->dur=width;int height=20+width/10;dim->heightsRef=dim->heightsPres={height-5,5,height};
        *first=0;*used=1;*before=*after=2;return 0;
    }
    dim->heightsRef=dim->heightsPres={20,10,30};dim->dur=error==2?-1:text.size()==3?50:70;
    *first=cp==0;*used=0;*before=cp==1&&text.size()==3?before_second:2;*after=cp==0&&text.size()==3?after_first:2;
    return 0;
}
static int WINAPI adjust(void*,int,int,LsCharRunInfo* run,LsNeighborInfo*,LsNeighborInfo*,int,int** left,int** right){for(int level=0;level<3;++level)for(int cp=0;cp<run->cwch;++cp){left[level][cp]=0;right[level][cp]=run->pwch[cp]==L' '?1000:0;}return 0;}
static int WINAPI compress(void*,int,int,LsCharRunInfo* run,LsNeighborInfo*,LsNeighborInfo*,int,int** left,int** right){for(int level=0;level<3;++level)for(int cp=0;cp<run->cwch;++cp){left[level][cp]=0;right[level][cp]=run->pwch[cp]==L' '?(level==0?2:level==1?4:8):0;}return 0;}
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
        using Session=int(WINAPI*)(void*,int,int,void*,void**,BOOL*);
        using Breaks=int(WINAPI*)(void*,int,void*,void*,void*,LsBreaks*,int*);
        using Query=int(WINAPI*)(void*,int,int,LsQSubInfo*,int*,LsTextCell*);
        using Display=int(WINAPI*)(void*,LSPOINT*,uint32_t,LSRECT*);
        using Dispose=int(WINAPI*)(void*,BOOL);
        using Destroy=int(WINAPI*)(void*);
#define PROC(name,type) auto name=reinterpret_cast<type>(GetProcAddress(dll,#name));require(name!=nullptr,#name)
        PROC(LoCreateParaBreakingSession,Session);PROC(LoCreateBreaks,Breaks);PROC(LoDisposeParaBreakingSession,Dispose);PROC(LoCreateContext,Create);PROC(LoSetDoc,Doc);PROC(LoCreateLine,Line);PROC(LoQueryLineCpPpoint,Query);PROC(LoDisplayLine,Display);PROC(LoDisposeLine,Dispose);PROC(LoDestroyContext,Destroy);
        require(sizeof(InlineInit)==24&&offsetof(InlineInit,pfnFormat)==8&&offsetof(InlineInit,pfnDraw)==16&&sizeof(ObjDim)==28,"original object callback record ABI");
        LsContextInfo config{};config.version=4;config.pfnFetchPap=reinterpret_cast<void*>(pap);config.pfnGetRunTextMetrics=reinterpret_cast<void*>(metrics);config.pfnGetRunCharWidths=reinterpret_cast<void*>(widths);config.pfnDrawTextRun=reinterpret_cast<void*>(draw_text);config.pfnGetObjectHandlerInfo=reinterpret_cast<void*>(handler);config.pfnGetCharExpansionInfoFullMixed=reinterpret_cast<void*>(adjust);config.pfnGetCharCompressionInfoFullMixed=reinterpret_cast<void*>(compress);
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
            require(LoCreateLine(context,0,0,1,8,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==3&&measured.upStartTrailing==104&&measured.upMinStartTrailing==(allowed?54:104),"min/max respects source object break table and atomic widths");
            require(LoDisposeLine(line,FALSE)==0,"object min/max line disposal");
        }
        using SetTabs=int(WINAPI*)(void*,int,int,const LsTbd*);PROC(LoSetTabs,SetTabs);
        text=L"a\t\ufffcb.cd\u2029";flexible=true;
        for(int alignment:{0,1,2,4}) {
            LsTbd tab{alignment,600,0,L'.'};formats=0;
            require(LoSetTabs(context,80,1,&tab)==0&&LoCreateLine(context,0,0,800,0,nullptr,&info,&line,&depth,&measured)==0&&info.cpLimToContinue==int(text.size()),"position-dependent object and aligned field settle on the complete source");
            LsQSubInfo sub{};LsTextCell cell{};require(LoQueryLineCpPpoint(line,2,1,&sub,&depth,&cell)==0,"settled object query");
            int expected=alignment==0?100:alignment==1?146:alignment==2?239:209;
            int start=alignment==0?600:alignment==1?507:alignment==2?321:381;
            int height=20+expected/10;
            std::printf("aligned native kind=%d pos=%d width=%d last=%d formats=%d ascent=%d descent=%d height=%d\n",alignment,cell.pointUvStartCell.x,cell.dupCell,last_position,formats,info.dvpAscent,info.dvpDescent,info.dvpMultiLineHeight);
            require(cell.pointUvStartCell.x==start&&cell.dupCell==expected&&last_position==start&&formats<=64&&info.dvpAscent==height-5&&info.dvpDescent==8&&info.dvpMultiLineHeight==height+3,"exact settled object pen/width, bounded format calls and updated line ascent/descent/height");
            draws=0;require(LoDisplayLine(line,&origin,1,&clip)==0&&draws==1&&object_origin.x==start+10,"settled object draw uses accepted pen and dimensions");
            require(LoDisposeLine(line,FALSE)==0,"settled object disposal");
        }
        LsTbd right_tab{2,600,0,L'.'};require(LoSetTabs(context,80,1,&right_tab)==0,"failure-case right tab");
        for(int bad:{1,2}){flexible_failure=bad;formats=0;line=reinterpret_cast<void*>(1);require(LoCreateLine(context,0,0,800,0,nullptr,&info,&line,&depth,&measured)==(bad==1?LS_ClientAbort:LS_InvalidFormatterResult)&&!line&&formats<=65,"callback error and oscillating position-dependent geometry fail with empty outputs and bounded work");}
        flexible_failure=0;formats=0;require(LoCreateLine(context,0,0,800,0,nullptr,&info,&line,&depth,&measured)==0&&LoDisposeLine(line,FALSE)==0,"object callback failure leaves context reusable");
        std::puts("native aligned flexible objects: exact anchors/pens/widths/final heights, convergence bounds, populated failure/oscillation isolation and context reuse PASS");
        require(LoSetTabs(context,0,0,nullptr)==0,"clear tabs before object justification");
        text=L"a a \ufffc b b c c d d e e\u2029";justify=true;
        for(bool kind:{false,true}) {
            mixed=kind;object_column=300;formats=0;
            require(LoCreateLine(context,0,10,300,0,nullptr,&info,&line,&depth,&measured)==0,"justified flexible object creation");
            LsQSubInfo sub{};LsTextCell cell{};require(LoQueryLineCpPpoint(line,4,1,&sub,&depth,&cell)==0,"justified object query");
            std::printf("justified native mixed=%d pen=%d width=%d last=%d formats=%d total=%d trailing=%d height=%d\n",mixed,cell.pointUvStartCell.x,cell.dupCell,last_position,formats,measured.upLimLine,measured.upStartTrailing,info.dvpMultiLineHeight);
            require(cell.pointUvStartCell.x==104&&cell.dupCell==98&&last_position==104&&formats<=130&&measured.upStartTrailing==300&&measured.upLimLine==310&&measured.upMinStartTrailing==(mixed?188:212)&&info.dvpAscent==24&&info.dvpDescent==8&&info.dvpMultiLineHeight==32,"exact settled InterWord/FullMixed object pen, width, line minima, height and right edge");
            draws=0;require(LoDisplayLine(line,&origin,1,&clip)==0&&draws==1&&object_origin.x==114,"justified object draw retains accepted geometry");require(LoDisposeLine(line,FALSE)==0,"justified object disposal");
        }
        object_column=200;formats=0;
        require(LoCreateLine(context,0,16,200,0,nullptr,&info,&line,&depth,&measured)==0,"compressed flexible object creation");
        LsQSubInfo compressed_sub{};LsTextCell compressed_cell{};require(LoQueryLineCpPpoint(line,4,1,&compressed_sub,&depth,&compressed_cell)==0,"compressed object query");
        std::printf("compressed native pen=%d width=%d last=%d formats=%d total=%d trailing=%d height=%d\n",compressed_cell.pointUvStartCell.x,compressed_cell.dupCell,last_position,formats,measured.upLimLine,measured.upStartTrailing,info.dvpMultiLineHeight);
        require(compressed_cell.pointUvStartCell.x==36&&compressed_cell.dupCell==82&&last_position==36&&measured.upStartTrailing==200&&measured.upLimLine==210&&measured.upMinStartTrailing==174&&measured.upMinLimLine==184&&info.dvpAscent==23&&info.dvpMultiLineHeight==31,"compression settles dependent object pen, dimensions, right edge and final heights");
        draws=0;require(LoDisplayLine(line,&origin,1,&clip)==0&&draws==1&&object_origin.x==46&&LoDisposeLine(line,FALSE)==0,"compressed accepted object drawing and disposal");
        object_column=300;
        for(int bad:{1,3}){flexible_failure=bad;formats=0;line=reinterpret_cast<void*>(1);require(LoCreateLine(context,0,10,300,0,nullptr,&info,&line,&depth,&measured)==(bad==1?LS_ClientAbort:LS_InvalidFormatterResult)&&!line&&formats<=130,"justification remeasurement failure/oscillation preserves empty output and bounded work");}
        flexible_failure=0;formats=0;require(LoCreateLine(context,0,10,300,0,nullptr,&info,&line,&depth,&measured)==0&&LoDisposeLine(line,FALSE)==0,"failed justified object leaves reusable context");
        object_column=200;formats=0;void* session=nullptr;BOOL session_justified=FALSE;
        require(LoCreateParaBreakingSession(context,0,200,nullptr,&session,&session_justified)==0&&session_justified,"dependent object paragraph session");
        LsBreaks candidates{};int best=0;
        require(LoCreateBreaks(context,0,nullptr,session,nullptr,&candidates,&best)==0,"dependent compressed candidate enumeration");
        void* retained=nullptr;for(int at=0;at<candidates.cBreaks;++at)if(candidates.plslinfoArray[at].cpLimToContinue==16)retained=candidates.pplolineArray[at];
        require(retained!=nullptr,"previously overflowing object candidate becomes feasible");
        require(LoQueryLineCpPpoint(retained,4,1,&compressed_sub,&depth,&compressed_cell)==0&&compressed_cell.pointUvStartCell.x==36&&compressed_cell.dupCell==82,"candidate settles actual dependent object measurement");
        require(LoQueryLineCpPpoint(retained,14,1,&compressed_sub,&depth,&compressed_cell)==0&&compressed_cell.pointUvStartCell.x==190&&compressed_cell.dupCell==10,"settled candidate ends at its real justified column");
        for(int at=0;at<candidates.cBreaks;++at)require(LoDisposeLine(candidates.pplolineArray[at],FALSE)==0,"dispose dependent candidates");
        flexible_failure=4;formats=0;candidates.cBreaks=9;best=9;
        require(LoCreateBreaks(context,0,nullptr,session,nullptr,&candidates,&best)==LS_ClientAbort&&!candidates.cBreaks&&!candidates.pplolineArray&&best==0,"remeasurement failure rolls back all provisional paragraph candidates");flexible_failure=0;
        require(LoCreateBreaks(context,0,nullptr,session,nullptr,&candidates,&best)==0,"dependent object paragraph session reusable after failed candidate");
        for(int at=0;at<candidates.cBreaks;++at)require(LoDisposeLine(candidates.pplolineArray[at],FALSE)==0,"dispose recovered dependent candidates");
        require(LoDisposeParaBreakingSession(session,FALSE)==0,"dependent object session disposal");
        text=L"a a \ufffc b b \ufffc c c d d e e\u2029";object_column=500;
        for(bool kind:{false,true}) {
            mixed=kind;formats=0;
            require(LoCreateLine(context,0,16,500,0,nullptr,&info,&line,&depth,&measured)==0&&measured.upStartTrailing==500&&measured.upLimLine==510&&formats<=260,"two dependent objects settle one justified retained line");
            for(int cp:{4,10}){LsQSubInfo sub{};LsTextCell cell{};require(LoQueryLineCpPpoint(line,cp,1,&sub,&depth,&cell)==0&&cell.pointUvStartCell.x==object_positions[cp]&&cell.dupCell==(500-cell.pointUvStartCell.x)/2,"each dependent object retains its own actual pen and measured dimensions");}
            draws=0;require(LoDisplayLine(line,&origin,1,&clip)==0&&draws==2&&LoDisposeLine(line,FALSE)==0,"both settled objects draw their own accepted measurements");
        }
        text=L"a\t\ufffcb.cd\u2029";object_column=300;mixed=true;formats=0;LsTbd exact_right{2,300,0,L'.'};
        require(LoSetTabs(context,80,1,&exact_right)==0&&LoCreateLine(context,0,7,300,0,nullptr,&info,&line,&depth,&measured)==0&&info.endr==0&&measured.upStartTrailing==300,"already fitted aligned field needs no unsupported spacing adjustment");
        require(LoDisposeLine(line,FALSE)==0,"already fitted aligned object disposal");
        std::puts("native justified dependent objects: exact expansion/compression, nominal rebuild, accepted pens/heights/drawing, bounded oscillation and failure reuse PASS");
        require(LoDestroyContext(context)==0,"object context teardown");
        std::puts("native InlineInit/ObjDim ABI, format/draw identity/geometry, atomic query, all 16 source break combinations, errors and ownership PASS");return 0;
    } catch(const std::exception& e) { std::printf("FAIL: %s\n",e.what());return 1; }
}
