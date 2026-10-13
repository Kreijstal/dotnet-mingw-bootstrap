// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <atomic>
#include "LineServicesNative.h"
struct State {
    uint32_t version;
    int32_t estimated,priorities,handlers,breaking,incremental,tab_count,display,equal;
    LsDevRes resolution;
    void* fetch_run;
};
static int WINAPI callback() { return 0; }
static void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main(int argc,char** argv) {
    try {
        require(argc==2,"usage: check DLL");
        HMODULE dll=LoadLibraryA(argv[1]); require(dll!=nullptr,"native DLL load");
#define PROC(name,type) auto name=reinterpret_cast<type>(GetProcAddress(dll,#name)); require(name!=nullptr,#name)
        using Create=int (WINAPI *)(LsContextInfo*,LscbkRedefined*,void**);
        using Destroy=int (WINAPI *)(void*);
        using Breaking=int (WINAPI *)(void*,int);
        using Doc=int (WINAPI *)(void*,int,int,const LsDevRes*);
        using Tabs=int (WINAPI *)(void*,int,int,const LsTbd*);
        using Snapshot=int (WINAPI *)(void*,State*);
        using Esc=void (WINAPI *)(EscStringInfo*);
        PROC(LoCreateContext,Create); PROC(LoDestroyContext,Destroy); PROC(LoSetBreaking,Breaking);
        PROC(LoSetDoc,Doc); PROC(LoSetTabs,Tabs); PROC(WpfGetLineServicesContextState,Snapshot); PROC(LoGetEscString,Esc);
        EscStringInfo escapes{}; LoGetEscString(&escapes);
        require(*static_cast<WCHAR*>(escapes.szParaSeparator)==0x2029&&*static_cast<WCHAR*>(escapes.szLineSeparator)==0x2028&&
                *static_cast<WCHAR*>(escapes.szNbsp)==0xA0,"Unicode escape/control vocabulary");
        void* pointer=reinterpret_cast<void*>(callback);
        LsContextInfo info{}; info.version=4; info.cEstimatedCharsPerLine=80; info.cJustPriorityLim=3;
        info.pfnFetchPap=info.pfnGetRunTextMetrics=info.pfnGetRunCharWidths=pointer;
        LscbkRedefined callbacks{pointer,pointer,pointer};
        LsIMethods handler{}; handler.pfnFmt=pointer;
        info.cInstalledHandlers=1; info.plsimethods=&handler;
        void* context=nullptr;
        require(LoCreateContext(&info,&callbacks,&context)==LS_None&&context&&info.pols==context,"owned context creation");
        info.version=0; callbacks.pfnFetchRunRedefined=nullptr; handler.pfnFmt=nullptr;
        State state{}; require(WpfGetLineServicesContextState(context,&state)==0&&state.version==4&&state.fetch_run==pointer&&state.handlers==1,"copied input/handler/callback state");
        LsDevRes resolution{1440,1440,1440,1440};
        require(LoSetDoc(context,1,1,&resolution)==0,"device setup");
        resolution.dxpInch=0; require(LoSetDoc(context,1,1,&resolution)==LS_InvalidDeviceResolution,"invalid device rejection");
        require(LoSetBreaking(context,1)==0&&LoSetBreaking(context,2)==LS_InvalidParameter,"breaking strategy bounds");
        LsTbd tabs[2]={{0,100,0,9},{2,200,0,9}};
        require(LoSetTabs(context,60,2,tabs)==0,"owned tab setup");
        tabs[1].ur=50; require(LoSetTabs(context,60,2,tabs)==LS_InvalidParameter,"unordered tabs rejected");
        require(WpfGetLineServicesContextState(context,&state)==0&&state.resolution.dxpInch==1440&&state.breaking==1&&state.incremental==60&&state.tab_count==2&&state.display&&state.equal,"retained configuration and failed-update isolation");
        std::atomic<bool> stable{true};
        std::thread reader([&]{ for(int n=0;n<1000;++n) { State other{}; if(WpfGetLineServicesContextState(context,&other)!=0||other.version!=4) stable=false; } });
        for(int n=0;n<1000;++n) require(LoSetBreaking(context,n%2)==0,"concurrent configuration");
        reader.join(); require(stable,"concurrent context lifetime");
        require(LoDestroyContext(context)==0&&LoDestroyContext(context)==LS_InvalidContext,"destroy/double-destroy");
        require(LoSetDoc(context,1,1,&resolution)==LS_InvalidContext,"stale context rejection");
        context=reinterpret_cast<void*>(1); require(LoCreateContext(nullptr,nullptr,&context)==LS_InvalidParameter&&!context,"invalid creation output cleared");
        require(LoCreateContext(nullptr,nullptr,nullptr)==LS_NullOutputParameter,"null output rejection");
        std::printf("ABI LsContextInfo %zu pols %zu pap %zu last %zu; callbacks %zu tabs %zu state %zu\n",sizeof(LsContextInfo),offsetof(LsContextInfo,pols),offsetof(LsContextInfo,pfnFetchPap),offsetof(LsContextInfo,fDontReleaseRuns),sizeof(LscbkRedefined),sizeof(LsTbd),sizeof(State));
        std::puts("Line Services owned contexts, configuration, Unicode controls, concurrency and invalid-input checks PASS");
        return 0;
    } catch(const std::exception& failure) { std::printf("FAIL: %s\n",failure.what()); return 1; }
}
