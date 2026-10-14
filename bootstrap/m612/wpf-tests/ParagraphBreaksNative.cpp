// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "LineServicesNative.h"
static std::wstring text=L"aaa bb cc\u2029";
static bool fail=false;
static uint32_t flags=0x40;
static int WINAPI pap(void*,int,LsPap* p){*p={};p->grpf=flags;p->cpFirst=0;return 0;}
static int WINAPI props(void*,int,int,LsLineProps* p){*p={};return 0;}
static int WINAPI fetch(void*,int cp,int,void*,WCHAR* buffer,int capacity,int* used,WCHAR** out,int* count,int* hidden,LsChp* p,uintptr_t* id){if(fail)return LS_ClientAbort;if(cp<0||cp>=int(text.size()))return LS_WrongGivenCp;*count=cp<int(text.size())-1?int(text.size())-1-cp:1;*hidden=0;*p={};p->idObj=0xFFFF;*id=1;*used=0;*out=nullptr;if(capacity>=*count){std::memcpy(buffer,text.data()+cp,*count*2);*used=1;}return 0;}
static int WINAPI metrics(void*,uint32_t,int,int,LsTxM* p){*p={10,5,15,0};return 0;}
static int WINAPI widths(void*,uint32_t,int,WCHAR*,int count,int,int,int* values,int* total,int* fitted){for(int i=0;i<count;++i)values[i]=10;*total=count*10;*fitted=count;return 0;}
static void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
int main(int argc,char** argv){try {
 check(argc==2,"usage: test DLL");HMODULE dll=LoadLibraryA(argv[1]);check(dll!=nullptr,"load DLL");
 using Create=int(WINAPI*)(LsContextInfo*,LscbkRedefined*,void**);using Doc=int(WINAPI*)(void*,int,int,const LsDevRes*);
 using Session=int(WINAPI*)(void*,int,int,void*,void**,BOOL*);using Breaks=int(WINAPI*)(void*,int,void*,void*,void*,LsBreaks*,int*);
 using Destroy=int(WINAPI*)(void*);using Dispose=int(WINAPI*)(void*,BOOL);using Acquire=int(WINAPI*)(void*,void**);
 using Line=int(WINAPI*)(void*,int,int,int,uint32_t,void*,LsLInfo*,void**,int*,LsLineWidths*);
#define PROC(name,type) auto name=reinterpret_cast<type>(GetProcAddress(dll,#name));check(name!=nullptr,#name)
 PROC(LoCreateContext,Create);PROC(LoSetDoc,Doc);PROC(LoDestroyContext,Destroy);PROC(LoCreateParaBreakingSession,Session);
 PROC(LoDisposeParaBreakingSession,Dispose);PROC(LoCreateBreaks,Breaks);PROC(LoDisposeLine,Dispose);PROC(LoCreateLine,Line);
 PROC(LoAcquireBreakRecord,Acquire);PROC(LoCloneBreakRecord,Acquire);PROC(LoDisposeBreakRecord,Dispose);PROC(LoRelievePenaltyResource,Destroy);
 static_assert(sizeof(LsBreaks)==32,"original x64 LsBreaks ABI");
 LsContextInfo info{};info.version=4;info.pfnFetchPap=reinterpret_cast<void*>(pap);info.pfnGetRunTextMetrics=reinterpret_cast<void*>(metrics);info.pfnGetRunCharWidths=reinterpret_cast<void*>(widths);
 LscbkRedefined callbacks{reinterpret_cast<void*>(fetch),nullptr,reinterpret_cast<void*>(props)};void* context=nullptr;
 check(LoCreateContext(&info,&callbacks,&context)==0,"context");LsDevRes res{1440,1440,1440,1440};check(LoSetDoc(context,0,1,&res)==0,"doc");
 void* session=reinterpret_cast<void*>(1);BOOL justified=TRUE;
 check(LoCreateParaBreakingSession(context,0,0,nullptr,&session,&justified)==LS_InvalidParameter && !session && !justified,"invalid width clears outputs");
 fail=true;check(LoCreateParaBreakingSession(context,0,65,nullptr,&session,&justified)==LS_ClientAbort && !session,"session propagates source failure");fail=false;
 check(LoCreateParaBreakingSession(context,0,65,nullptr,&session,&justified)==0 && session && !justified,"session prefetch");
 LsBreaks breaks{};int best=-1;check(LoCreateBreaks(context,0,nullptr,session,nullptr,&breaks,&best)==0,"break enumeration");
 check(breaks.cBreaks==2 && best==1,"two natural feasible alternatives");
 check(breaks.plslinfoArray[0].cpLimToContinue==4 && breaks.plslinfoArray[1].cpLimToContinue==7,"actual word boundaries");
 std::vector<void*> handles(breaks.pplolineArray,breaks.pplolineArray+breaks.cBreaks);void* br=nullptr;
 check(breaks.plinepenaltyArray[0] && breaks.plinepenaltyArray[1] && breaks.plinepenaltyArray[0]!=breaks.plinepenaltyArray[1],"distinct real penalty resources");
 check(LoRelievePenaltyResource(handles[1])==0 && LoRelievePenaltyResource(handles[1])==0,"detach penalty lifetime");
 check(LoAcquireBreakRecord(handles[1],&br)==0,"break record");void* clone=nullptr;check(LoCloneBreakRecord(br,&clone)==0,"clone record");check(LoDisposeBreakRecord(br,FALSE)==0,"dispose original record");
 for(void* key:handles)check(LoDisposeLine(key,FALSE)==0,"dispose candidates");
 check(LoCreateBreaks(context,7,clone,session,nullptr,&breaks,&best)==0 && breaks.cBreaks==1 && breaks.plslinfoArray[0].cpLimToContinue==10 && breaks.plslinfoArray[0].endr==2,"continue from independent record to paragraph end");
 void* last=breaks.pplolineArray[0];
 fail=true;breaks.cBreaks=9;best=9;check(LoCreateBreaks(context,0,nullptr,session,nullptr,&breaks,&best)==LS_ClientAbort && !breaks.cBreaks && !breaks.pplolineArray && best==0,"failed enumeration is transactional");fail=false;
 check(LoDisposeParaBreakingSession(session,FALSE)==0,"dispose session with live candidate");check(LoDisposeLine(last,FALSE)==0,"candidate outlives session");check(LoDisposeBreakRecord(clone,FALSE)==0,"dispose cloned record");
 check(LoCreateBreaks(context,0,nullptr,session,nullptr,&breaks,&best)==LS_InvalidParameter && !breaks.cBreaks,"stale session rejected");
 text=L"abcdefghij\u2029";check(LoCreateParaBreakingSession(context,0,25,nullptr,&session,&justified)==0,"long word session");
 check(LoCreateBreaks(context,0,nullptr,session,nullptr,&breaks,&best)==0 && breaks.cBreaks==1 && breaks.plslinfoArray[0].cpLimToContinue==2 && breaks.plslinfoArray[0].fForcedBreak,"grapheme fallback marked forced");
 check(LoDisposeLine(breaks.pplolineArray[0],FALSE)==0,"dispose forced");check(LoDisposeParaBreakingSession(session,TRUE)==0,"finalizer disposal");
 flags|=0x80000;check(LoCreateParaBreakingSession(context,0,25,nullptr,&session,&justified)==0,"overflow policy session");
 check(LoCreateBreaks(context,0,nullptr,session,nullptr,&breaks,&best)==0 && breaks.cBreaks==1 && breaks.plslinfoArray[0].cpLimToContinue==11 && breaks.plslinfoArray[0].endr==2 && !breaks.plslinfoArray[0].fForcedBreak,"WrapWithOverflow retains indivisible word and paragraph end without forced flag");
 check(LoDisposeLine(breaks.pplolineArray[0],FALSE)==0,"dispose overflow");check(LoDisposeParaBreakingSession(session,FALSE)==0,"dispose overflow session");
 check(LoDestroyContext(context)==0,"destroy context");FreeLibrary(dll);std::puts("PASS original paragraph session/break ABI, all feasible word breaks, penalties, continuation, transactional failures and lifetime");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
