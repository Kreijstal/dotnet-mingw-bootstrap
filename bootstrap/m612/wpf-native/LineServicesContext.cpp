// SPDX-License-Identifier: MIT
// Source Line Services context/configuration. Line formatting stays absent
// until implemented; these APIs keep real owned state for that formatter.
#include <windows.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <new>
#include "LineServicesNative.h"

namespace {
struct Context {
    LsContextInfo info;
    LscbkRedefined callbacks;
    std::vector<LsIMethods> handlers;
    LsDevRes resolution{};
    bool display=false,equal_devices=false,has_document=false;
    int breaking=0,incremental_tab=0;
    std::vector<LsTbd> tabs;
};
std::mutex gate;
std::unordered_map<void*,std::shared_ptr<Context>> contexts;

// The source formatter's control vocabulary uses stable, single Unicode
// characters. A future line engine interprets these same markers explicitly.
WCHAR paragraph[]={0x2029,0},line[]={0x2028,0},hidden[]={0x2060,0},nbsp[]={0x00A0,0};
WCHAR object_end[]={0xFFFB,0},object_replacement[]={0xFFFC,0};
}

extern "C" __declspec(dllexport) void WINAPI LoGetEscString(EscStringInfo* output) {
    if(output) *output={paragraph,line,hidden,nbsp,object_end,object_replacement};
}

extern "C" __declspec(dllexport) int WINAPI LoCreateContext(LsContextInfo* info,LscbkRedefined* callbacks,void** output) {
    if(!output) return LS_NullOutputParameter; *output=nullptr;
    if(!info || !callbacks || info->version!=4 || info->cInstalledHandlers<0 || info->cInstalledHandlers>1024 ||
       (info->cInstalledHandlers && !info->plsimethods) || info->cEstimatedCharsPerLine<0 || info->cJustPriorityLim<0 ||
       !callbacks->pfnFetchRunRedefined || !callbacks->pfnFetchLineProps || !info->pfnFetchPap ||
       !info->pfnGetRunTextMetrics || !info->pfnGetRunCharWidths) return LS_InvalidParameter;
    try {
        auto context=std::make_shared<Context>();
        context->info=*info; context->callbacks=*callbacks;
        if(info->cInstalledHandlers) {
            auto* handlers=static_cast<LsIMethods*>(info->plsimethods);
            context->handlers.assign(handlers,handlers+info->cInstalledHandlers);
            context->info.plsimethods=context->handlers.data();
        }
        void* handle=context.get();
        context->info.pols=handle;
        std::lock_guard<std::mutex> lock(gate);
        contexts.emplace(handle,context);
        info->pols=handle; *output=handle;
        return LS_None;
    } catch(const std::bad_alloc&) { return LS_OutOfMemory; }
}

extern "C" __declspec(dllexport) int WINAPI LoDestroyContext(void* handle) {
    std::lock_guard<std::mutex> lock(gate);
    auto found=contexts.find(handle);
    if(found==contexts.end()) return LS_InvalidContext;
    contexts.erase(found); return LS_None;
}

extern "C" __declspec(dllexport) int WINAPI LoSetBreaking(void* handle,int strategy) {
    std::lock_guard<std::mutex> lock(gate);
    auto found=contexts.find(handle); if(found==contexts.end()) return LS_InvalidContext;
    if(strategy<0 || strategy>1) return LS_InvalidParameter;
    found->second->breaking=strategy; return LS_None;
}

extern "C" __declspec(dllexport) int WINAPI LoSetDoc(void* handle,int display,int equal_devices,const LsDevRes* resolution) {
    std::lock_guard<std::mutex> lock(gate);
    auto found=contexts.find(handle); if(found==contexts.end()) return LS_InvalidContext;
    if(!resolution || !resolution->dxpInch || !resolution->dypInch || !resolution->dxrInch || !resolution->dyrInch)
        return LS_InvalidDeviceResolution;
    auto& context=*found->second;
    context.resolution=*resolution; context.display=display!=0; context.equal_devices=equal_devices!=0;
    context.has_document=true; return LS_None;
}

extern "C" __declspec(dllexport) int WINAPI LoSetTabs(void* handle,int incremental,int count,const LsTbd* tabs) {
    std::lock_guard<std::mutex> lock(gate);
    auto found=contexts.find(handle); if(found==contexts.end()) return LS_InvalidContext;
    if(incremental<0 || count<0 || count>1048576 || (count && !tabs)) return LS_InvalidParameter;
    for(int i=0;i<count;++i) {
        if(tabs[i].lskt<0 || tabs[i].lskt>4 || (i && tabs[i].ur<tabs[i-1].ur)) return LS_InvalidParameter;
    }
    try {
        std::vector<LsTbd> replacement;
        if(count) replacement.assign(tabs,tabs+count);
        found->second->tabs.swap(replacement); found->second->incremental_tab=incremental;
        return LS_None;
    } catch(const std::bad_alloc&) { return LS_OutOfMemory; }
}

// Explicit source diagnostic ABI: copy current state, without exposing mutable
// registry-owned data or pretending that a line has already been formatted.
struct ContextState {
    uint32_t version;
    int32_t estimated,priorities,handlers,breaking,incremental_tab,tab_count,display,equal_devices;
    LsDevRes resolution;
    void* fetch_run;
};
extern "C" __declspec(dllexport) int WINAPI WpfGetLineServicesContextState(void* handle,ContextState* output) {
    if(!output) return LS_NullOutputParameter; *output={};
    std::lock_guard<std::mutex> lock(gate);
    auto found=contexts.find(handle); if(found==contexts.end()) return LS_InvalidContext;
    const auto& value=*found->second;
    *output={value.info.version,value.info.cEstimatedCharsPerLine,value.info.cJustPriorityLim,value.info.cInstalledHandlers,
             value.breaking,value.incremental_tab,static_cast<int32_t>(value.tabs.size()),value.display,value.equal_devices,
             value.resolution,value.callbacks.pfnFetchRunRedefined};
    return LS_None;
}
