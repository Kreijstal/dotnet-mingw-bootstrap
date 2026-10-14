// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "LineServicesNative.h"
static bool rtl=false,abort_marker=false,abort_info=false;
static int draws=0,marker_draws=0,source_units=0;
static int WINAPI pap(void*,int,LsPap* p){*p={};p->cpFirst=0;p->grpf=0x1040;p->lstflow=rtl?4:0;return 0;}
static int WINAPI props(void*,int,int first,LsLineProps* p){*p={};p->durLeft=first?-20:0;return 0;}
static int WINAPI number(void*,int* align,LsChp* p,uintptr_t* id,uint16_t* added,LsChp* ap,uintptr_t* aid,int* word95,int* offset,int* width){if(abort_info)return LS_ClientAbort;*align=2;*p={};p->idObj=0xFFFF;*id=0x40000001;*added=L'\t';*ap=*p;*aid=*id;*word95=0;*offset=0;*width=0;return 0;}
static int WINAPI fetch(void*,int cp,int,void*,WCHAR* buffer,int capacity,int* used,WCHAR** out,int* count,int* hidden,LsChp* p,uintptr_t* id){bool marker=cp<0;if(marker&&abort_marker)return LS_ClientAbort;std::wstring text=marker?L"42.\u2029":L"abc\u2029";int at=marker?cp-LS_FirstMarkerCp:cp;if(at<0||at>=int(text.size()))return LS_WrongGivenCp;*count=at<3?3-at:1;*hidden=0;*p={};p->idObj=0xFFFF;*id=marker?0x40000001:1;*used=0;*out=nullptr;if(capacity>=*count){std::memcpy(buffer,text.data()+at,*count*2);*used=1;}return 0;}
static int WINAPI metrics(void*,uint32_t id,int,int,LsTxM* p){*p=id&0x40000000?LsTxM{99,1,100,0}:LsTxM{10,5,15,0};return 0;}
static int WINAPI widths(void*,uint32_t,int,WCHAR*,int count,int,int,int* values,int* total,int* fitted){for(int i=0;i<count;++i)values[i]=10;*total=10*count;*fitted=count;return 0;}
static int WINAPI draw(void*,uint32_t id,LSPOINT* point,WCHAR* text,int*,int count,int flow,uint32_t,LSPOINT*,LsHeights*,int width,LSRECT*){if(flow!=(rtl?4:0)||width!=10*count)return LS_ClientAbort;if(id&0x40000000){if(point->x!=(rtl?50:-50)||count!=3||text[0]!=L'4')return LS_ClientAbort;++marker_draws;}else if(point->x!=0)return LS_ClientAbort;++draws;return 0;}
static int WINAPI enumerate(void*,uint32_t id,int cp,int dcp,WCHAR*,int,int,int,int,LSPOINT*,LsHeights*,int,int,int*,uint16_t*,uint16_t*,uint16_t*,int*,void*,uint32_t*,int){if(cp<0||(id&0x40000000))return LS_ClientAbort;source_units+=dcp;return 0;}
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
 check(argc==2,"usage: check DLL");HMODULE dll=LoadLibraryA(argv[1]);check(dll!=nullptr,"load DLL");
 using Create=int(WINAPI*)(LsContextInfo*,LscbkRedefined*,void**);using Doc=int(WINAPI*)(void*,int,int,const LsDevRes*);using Tabs=int(WINAPI*)(void*,int,int,const LsTbd*);
 using Line=int(WINAPI*)(void*,int,int,int,uint32_t,void*,LsLInfo*,void**,int*,LsLineWidths*);using Dispose=int(WINAPI*)(void*,BOOL);using Destroy=int(WINAPI*)(void*);using Acquire=int(WINAPI*)(void*,void**);
 using Draw=int(WINAPI*)(void*,LSPOINT*,uint32_t,LSRECT*);using Enum=int(WINAPI*)(void*,BOOL,BOOL,LSPOINT*);using Query=int(WINAPI*)(void*,int,int,LsQSubInfo*,int*,LsTextCell*);
#define PROC(name,type) auto name=reinterpret_cast<type>(GetProcAddress(dll,#name));check(name!=nullptr,#name)
 PROC(LoCreateContext,Create);PROC(LoSetDoc,Doc);PROC(LoSetTabs,Tabs);PROC(LoCreateLine,Line);PROC(LoDisposeLine,Dispose);PROC(LoDestroyContext,Destroy);PROC(LoDisplayLine,Draw);PROC(LoEnumLine,Enum);PROC(LoQueryLineCpPpoint,Query);
 PROC(LoAcquireBreakRecord,Acquire);PROC(LoCloneBreakRecord,Acquire);PROC(LoDisposeBreakRecord,Dispose);
 for(bool direction:{false,true}){rtl=direction;draws=marker_draws=source_units=0;LsContextInfo cfg{};cfg.version=4;cfg.pfnFetchPap=reinterpret_cast<void*>(pap);cfg.pfnGetAutoNumberInfo=reinterpret_cast<void*>(number);cfg.pfnGetRunTextMetrics=reinterpret_cast<void*>(metrics);cfg.pfnGetRunCharWidths=reinterpret_cast<void*>(widths);cfg.pfnDrawTextRun=reinterpret_cast<void*>(draw);cfg.pfnEnumText=reinterpret_cast<void*>(enumerate);LscbkRedefined cb{reinterpret_cast<void*>(fetch),nullptr,reinterpret_cast<void*>(props)};void* context=nullptr;check(LoCreateContext(&cfg,&cb,&context)==0,"context");LsDevRes res{1440,1440,1440,1440};check(LoSetDoc(context,0,1,&res)==0,"doc");LsTbd stop{};stop.ur=0;check(LoSetTabs(context,40,1,&stop)==0,"marker-required text stop");
  LsLInfo info{};LsLineWidths geometry{};void* line=nullptr;int depth=0;check(LoCreateLine(context,0,0,100,0,nullptr,&info,&line,&depth,&geometry)==0,"marker line");
  check(info.cpLimToContinue==4&&info.dvpAscent==10&&info.dvpMultiLineHeight==15,"marker excluded from body source/height");check(geometry.upStartMarker==-50&&geometry.upLimMarker==-20&&geometry.upStartMainText==0&&geometry.upLimLine==30,"separate marker/body geometry");LSPOINT origin{};LSRECT clip{-100,-100,100,100};check(LoDisplayLine(line,&origin,1,&clip)==0&&draws==2&&marker_draws==1,"exact marker and body drawing");check(LoEnumLine(line,FALSE,FALSE,&origin)==0&&source_units==3,"body-only glyph enumeration");LsQSubInfo sub{};LsTextCell cell{};int qdepth=0;check(LoQueryLineCpPpoint(line,0,1,&sub,&qdepth,&cell)==0&&cell.dupCell==10&&cell.pointUvStartCell.x==0,"marker excluded from caret geometry");check(LoQueryLineCpPpoint(line,LS_FirstMarkerCp,1,&sub,&qdepth,&cell)==LS_WrongGivenCp,"negative marker is not backing source");check(LoDisposeLine(line,FALSE)==0,"dispose marker line");
  abort_info=true;check(LoCreateLine(context,0,0,100,0,nullptr,&info,&line,&depth,&geometry)==LS_ClientAbort&&!line&&info.cpLimToContinue==0,"number callback failure clears outputs");abort_info=false;abort_marker=true;check(LoCreateLine(context,0,0,100,0,nullptr,&info,&line,&depth,&geometry)==LS_ClientAbort&&!line,"marker fetch failure clears outputs");abort_marker=false;
  check(LoCreateLine(context,0,0,15,0,nullptr,&info,&line,&depth,&geometry)==0&&info.cpLimToContinue==1,"marker does not change wrapping");void* br=nullptr;void* clone=nullptr;check(LoAcquireBreakRecord(line,&br)==0&&LoCloneBreakRecord(br,&clone)==0,"independent marked continuation");check(LoDisposeLine(line,FALSE)==0&&LoDisposeBreakRecord(br,FALSE)==0,"dispose original line and record");marker_draws=draws=0;check(LoCreateLine(context,1,0,100,0,clone,&info,&line,&depth,&geometry)==0&&info.cpLimToContinue==4,"continue body after marker");check(LoDisplayLine(line,&origin,1,&clip)==0&&marker_draws==0&&draws==1,"continuation does not repeat marker");check(LoDisposeLine(line,FALSE)==0&&LoDisposeBreakRecord(clone,FALSE)==0&&LoDestroyContext(context)==0,"marker resources cleanup");
 }
 FreeLibrary(dll);std::puts("PASS original autonumber callback ABI, negative marker source, separate LTR/RTL origins, body-only metrics/carets/enumeration, errors and cloned continuation");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
