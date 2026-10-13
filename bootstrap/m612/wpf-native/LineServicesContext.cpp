// SPDX-License-Identifier: MIT
// Source Line Services context/configuration and callback-driven line state.
#include <windows.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <new>
#include <limits>
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
    bool in_use=false;
};

struct GlyphOffset { int32_t du,dv; };
struct Run {
    int cp=0,flow=0,width=0;
    uintptr_t id=0;
    LsChp properties{};
    LsTxM metrics{};
    std::vector<WCHAR> text;
    std::vector<int> widths,advances;
    std::vector<uint16_t> glyphs,clusters,characters;
    std::vector<uint32_t> glyph_properties;
    std::vector<uint8_t> expansion;
    std::vector<GlyphOffset> offsets;
    bool shaped=false,hidden=false;
};
struct Line {
    std::shared_ptr<Context> context;
    std::vector<Run> runs;
    LsLInfo info{};
    LsLineWidths widths{};
};
std::mutex gate;
std::unordered_map<void*,std::shared_ptr<Context>> contexts;
std::unordered_map<void*,std::shared_ptr<Line>> lines;

using FetchPap=int (WINAPI *)(void*,int,LsPap*);
using FetchProps=int (WINAPI *)(void*,int,int,LsLineProps*);
using FetchRun=int (WINAPI *)(void*,int,int,void*,WCHAR*,int,int*,WCHAR**,int*,int*,LsChp*,uintptr_t*);
using Metrics=int (WINAPI *)(void*,uint32_t,int,int,LsTxM*);
using Widths=int (WINAPI *)(void*,uint32_t,int,WCHAR*,int,int,int,int*,int*,int*);
using Glyphs=int (WINAPI *)(void*,uintptr_t*,int*,int,WCHAR*,int,int,uint16_t*,uint32_t*,int,int*,uint16_t*,uint16_t*,int*,int*);
using Positions=int (WINAPI *)(void*,uintptr_t*,int*,int,int,WCHAR*,uint16_t*,uint16_t*,int,uint16_t*,uint32_t*,int,int,int*,GlyphOffset*);
using DrawTextCallback=int (WINAPI *)(void*,uint32_t,LSPOINT*,WCHAR*,int*,int,int,uint32_t,LSPOINT*,LsHeights*,int,LSRECT*);
using DrawGlyphs=int (WINAPI *)(void*,uint32_t,WCHAR*,uint16_t*,uint16_t*,int,uint16_t*,int*,int*,GlyphOffset*,uint32_t*,uint8_t*,int,int,uint32_t,LSPOINT*,LsHeights*,int,LSRECT*);
using EnumText=int (WINAPI *)(void*,uint32_t,int,int,WCHAR*,int,int,int,int,LSPOINT*,LsHeights*,int,int,int*,uint16_t*,uint16_t*,uint16_t*,int*,GlyphOffset*,uint32_t*,int);

std::shared_ptr<Line> get_line(void* handle) {
    std::lock_guard<std::mutex> lock(gate);
    auto found=lines.find(handle); return found==lines.end()?nullptr:found->second;
}

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
    if(found->second->in_use) return LS_ContextInUse;
    contexts.erase(found); return LS_None;
}

extern "C" __declspec(dllexport) int WINAPI LoSetBreaking(void* handle,int strategy) {
    std::lock_guard<std::mutex> lock(gate);
    auto found=contexts.find(handle); if(found==contexts.end()) return LS_InvalidContext;
    if(found->second->in_use) return LS_ContextInUse;
    if(strategy<0 || strategy>1) return LS_InvalidParameter;
    found->second->breaking=strategy; return LS_None;
}

extern "C" __declspec(dllexport) int WINAPI LoSetDoc(void* handle,int display,int equal_devices,const LsDevRes* resolution) {
    std::lock_guard<std::mutex> lock(gate);
    auto found=contexts.find(handle); if(found==contexts.end()) return LS_InvalidContext;
    if(found->second->in_use) return LS_ContextInUse;
    if(!resolution || !resolution->dxpInch || !resolution->dypInch || !resolution->dxrInch || !resolution->dyrInch)
        return LS_InvalidDeviceResolution;
    auto& context=*found->second;
    context.resolution=*resolution; context.display=display!=0; context.equal_devices=equal_devices!=0;
    context.has_document=true; return LS_None;
}

extern "C" __declspec(dllexport) int WINAPI LoSetTabs(void* handle,int incremental,int count,const LsTbd* tabs) {
    std::lock_guard<std::mutex> lock(gate);
    auto found=contexts.find(handle); if(found==contexts.end()) return LS_InvalidContext;
    if(found->second->in_use) return LS_ContextInUse;
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

extern "C" __declspec(dllexport) int WINAPI LoCreateLine(void* handle,int cp,int limit,int column,uint32_t flags,
    void* previous_break,LsLInfo* info,void** output,int* depth,LsLineWidths* widths) {
    if(!info || !output || !depth || !widths) return LS_NullOutputParameter;
    *info={}; *output=nullptr; *depth=0; *widths={};
    if(cp<0 || limit<0 || column<0) return LS_InvalidParameter;
    std::shared_ptr<Context> context;
    {
        std::lock_guard<std::mutex> lock(gate);
        auto found=contexts.find(handle); if(found==contexts.end()) return LS_InvalidContext;
        context=found->second;
        if(context->in_use) return LS_ContextInUse;
        if(!context->has_document) return LS_InvalidDeviceResolution;
        context->in_use=true;
    }
    struct Active {
        std::shared_ptr<Context> context;
        ~Active() { std::lock_guard<std::mutex> lock(gate); context->in_use=false; }
    } active{context};
    // Break records, optimal breaking and inline objects require their own
    // implementations. Do not return a nominal line that ignores those inputs.
    if(previous_break || (flags&8)) return LS_NotImplemented;
    try {
        auto result=std::make_shared<Line>(); result->context=context;
        LsPap paragraph{}; LsLineProps properties{};
        int status=reinterpret_cast<FetchPap>(context->info.pfnFetchPap)(context->info.pols,cp,&paragraph);
        if(status) return status;
        status=reinterpret_cast<FetchProps>(context->callbacks.pfnFetchLineProps)(context->info.pols,cp,1,&properties);
        if(status) return status;
        if(paragraph.fJustify || (paragraph.grpf&0x1000)) return LS_NotImplemented;
        result->widths.upStartMainText=properties.durLeft;
        int position=cp,total=properties.durLeft,trailing=properties.durLeft;
        int flow=paragraph.lstflow;
        if(flow!=0 && flow!=4) return LS_NotImplemented;
        bool ended=false;
        for(int attempts=0;attempts<9600 && !ended;++attempts) {
            Run run; run.cp=position; run.flow=flow;
            std::vector<WCHAR> buffer(4096);
            int used=0,count=0,hidden_flag=0; WCHAR* text=nullptr;
            for(int retry=0;retry<3;++retry) {
                status=reinterpret_cast<FetchRun>(context->callbacks.pfnFetchRunRedefined)(context->info.pols,
                    position,0,nullptr,buffer.data(),static_cast<int>(buffer.size()),&used,&text,&count,&hidden_flag,&run.properties,&run.id);
                if(status) return status;
                if(count<=0 || count>9600) return LS_InvalidDcpFetched;
                if(used) { text=buffer.data(); break; }
                if(text) break;
                buffer.resize(count);
            }
            if(!text) return LS_InvalidCpContentFetched;
            if(run.properties.idObj!=0xFFFF) return LS_NotImplemented;
            if(std::find(text,text+count,L'\t')!=text+count) return LS_NotImplemented;
            run.hidden=hidden_flag!=0 || (run.properties.flags&0x10000)!=0;
            run.text.assign(text,text+count); run.widths.resize(count);
            if(count==1 && (text[0]==0x2028 || text[0]==0x2029)) {
                if(position==INT_MAX) return LS_InvalidFormatterResult;
                result->info.endr=text[0]==0x2029?2:4;
                ended=true; ++position; break;
            }
            if(limit>0 && position-cp+count>limit) count=limit-(position-cp);
            if(count<=0) { ended=true; break; }
            run.text.resize(count); run.widths.resize(count);
            if(!run.hidden) {
                status=reinterpret_cast<Metrics>(context->info.pfnGetRunTextMetrics)(context->info.pols,
                    static_cast<uint32_t>(run.id),0,flow,&run.metrics);
                if(status) return status;
                if(run.properties.flags&0x400) {
                    if(!context->callbacks.pfnGetGlyphsRedefined || !context->info.pfnGetGlyphPositions) return LS_NotImplemented;
                    run.shaped=true; run.clusters.resize(count); run.characters.resize(count);
                    std::vector<int> alone(count);
                    int capacity=count+count/2+16,actual=0; bool generated=false;
                    for(int retry=0;retry<3;++retry) {
                        run.glyphs.resize(capacity); run.glyph_properties.resize(capacity);
                        int used_glyphs=0;
                        status=reinterpret_cast<Glyphs>(context->callbacks.pfnGetGlyphsRedefined)(context->info.pols,
                            &run.id,&count,1,run.text.data(),count,flow,run.glyphs.data(),run.glyph_properties.data(),capacity,
                            &used_glyphs,run.clusters.data(),run.characters.data(),alone.data(),&actual);
                        if(status) return status;
                        if(used_glyphs) { generated=true; break; }
                        if(actual<=capacity || actual>65535) return LS_InvalidCharToGlyphMapping;
                        capacity=actual;
                    }
                    if(!generated || actual<=0 || actual>capacity) return LS_InvalidCharToGlyphMapping;
                    run.glyphs.resize(actual); run.glyph_properties.resize(actual);
                    run.advances.resize(actual); run.offsets.resize(actual);
                    run.expansion.resize(actual);
                    status=reinterpret_cast<Positions>(context->info.pfnGetGlyphPositions)(context->info.pols,&run.id,&count,1,0,
                        run.text.data(),run.clusters.data(),run.characters.data(),count,run.glyphs.data(),run.glyph_properties.data(),actual,
                        flow,run.advances.data(),run.offsets.data());
                    if(status) return status;
                    int64_t advance=0;
                    for(int value:run.advances) advance+=value;
                    if(advance<0 || advance>INT_MAX) return LS_InvalidFormatterResult;
                    run.width=static_cast<int>(advance);
                } else {
                    int measured=0,fitted=0;
                    status=reinterpret_cast<Widths>(context->info.pfnGetRunCharWidths)(context->info.pols,
                        static_cast<uint32_t>(run.id),0,run.text.data(),count,INT_MAX,flow,run.widths.data(),&measured,&fitted);
                    if(status) return status;
                    if(fitted!=count || measured<0) return LS_InvalidFormatterResult;
                    run.width=measured;
                }
                result->info.dvpAscent=std::max(result->info.dvpAscent,run.metrics.dvAscent);
                result->info.dvpDescent=std::max(result->info.dvpDescent,run.metrics.dvDescent);
                result->info.dvpMultiLineHeight=std::max(result->info.dvpMultiLineHeight,run.metrics.dvMultiLineHeight);
            }
            if(static_cast<int64_t>(total)+run.width>INT_MAX || static_cast<int64_t>(position)+count>INT_MAX) return LS_InvalidFormatterResult;
            total+=run.width; position+=count;
            if((paragraph.grpf&0x40) && total>column-properties.durRightBreak) return LS_NotImplemented;
            // Track trailing nominal spaces; shaped runs keep their full width
            // until cluster-aware whitespace trimming is implemented.
            trailing=total;
            if(!run.shaped) for(int i=count-1;i>=0 && (run.text[i]==L' '||run.text[i]==0xA0);--i) trailing-=run.widths[i];
            result->info.EffectsFlags|=run.properties.effectsFlags;
            result->runs.push_back(std::move(run));
            if(limit>0 && position-cp>=limit) ended=true;
        }
        if(!ended) return LS_TooLongParagraph;
        result->info.dvrAscent=result->info.dvpAscent;
        result->info.dvrDescent=result->info.dvpDescent;
        result->info.dvrMultiLineHeight=result->info.dvpMultiLineHeight;
        if(result->runs.empty()) result->info.dvrMultiLineHeight=INT_MAX;
        result->info.cpLimToContinue=result->info.cpLimToStay=position;
        result->info.cpFirstVis=cp; result->info.fFirstLineInPara=1;
        result->widths.upLimLine=total; result->widths.upStartTrailing=trailing;
        result->widths.upMinLimLine=total; result->widths.upMinStartTrailing=trailing;
        void* line_handle=result.get();
        { std::lock_guard<std::mutex> lock(gate); lines.emplace(line_handle,result); }
        *info=result->info; *widths=result->widths; *depth=1; *output=line_handle;
        return LS_None;
    } catch(const std::bad_alloc&) { return LS_OutOfMemory; }
}

extern "C" __declspec(dllexport) int WINAPI LoDisposeLine(void* handle,BOOL) {
    std::lock_guard<std::mutex> lock(gate);
    auto found=lines.find(handle); if(found==lines.end()) return LS_InvalidLine;
    lines.erase(found); return LS_None;
}

extern "C" __declspec(dllexport) int WINAPI LoDisplayLine(void* handle,LSPOINT* origin,uint32_t mode,LSRECT* clip) {
    if(!origin || !clip) return LS_InvalidParameter;
    auto line=get_line(handle); if(!line) return LS_InvalidLine;
    auto& context=*line->context;
    int x=origin->x+line->widths.upStartMainText;
    for(auto& run:line->runs) {
        LSPOINT point{x,origin->y};
        LsHeights height{run.metrics.dvAscent,run.metrics.dvDescent,run.metrics.dvMultiLineHeight};
        int status=0;
        if(!run.hidden) {
            if(run.properties.flags&0x60000) return LS_NotImplemented;
            if(run.shaped) {
                if(!context.info.pfnDrawGlyphs) return LS_NotImplemented;
                status=reinterpret_cast<DrawGlyphs>(context.info.pfnDrawGlyphs)(context.info.pols,static_cast<uint32_t>(run.id),
                    run.text.data(),run.clusters.data(),run.characters.data(),static_cast<int>(run.text.size()),run.glyphs.data(),
                    run.advances.data(),run.advances.data(),run.offsets.data(),run.glyph_properties.data(),run.expansion.data(),
                    static_cast<int>(run.glyphs.size()),run.flow,mode,&point,&height,run.width,clip);
            } else {
                if(!context.info.pfnDrawTextRun) return LS_NotImplemented;
                status=reinterpret_cast<DrawTextCallback>(context.info.pfnDrawTextRun)(context.info.pols,static_cast<uint32_t>(run.id),
                    &point,run.text.data(),run.widths.data(),static_cast<int>(run.text.size()),run.flow,mode,&point,&height,run.width,clip);
            }
            if(status) return status;
        }
        x+=run.width;
    }
    return LS_None;
}

extern "C" __declspec(dllexport) int WINAPI LoEnumLine(void* handle,BOOL reverse,BOOL geometry,LSPOINT* origin) {
    if(!origin) return LS_InvalidParameter;
    auto line=get_line(handle); if(!line) return LS_InvalidLine;
    const auto& context=*line->context;
    if(!context.info.pfnEnumText) return LS_NotImplemented;
    try {
    std::vector<int> starts;
    int x=origin->x+line->widths.upStartMainText;
    for(const auto& run:line->runs) { starts.push_back(x); x+=run.width; }
    for(size_t index=0;index<line->runs.size();++index) {
        size_t position=reverse?line->runs.size()-index-1:index;
        auto& run=line->runs[position]; if(run.hidden) continue;
        LSPOINT point{starts[position],origin->y};
        LsHeights height{run.metrics.dvAscent,run.metrics.dvDescent,run.metrics.dvMultiLineHeight};
        int status=reinterpret_cast<EnumText>(context.info.pfnEnumText)(context.info.pols,static_cast<uint32_t>(run.id),
            run.cp,static_cast<int>(run.text.size()),run.text.data(),static_cast<int>(run.text.size()),run.flow,
            reverse!=FALSE,geometry!=FALSE,&point,&height,run.width,run.shaped?1:0,run.widths.data(),
            run.shaped?run.clusters.data():nullptr,run.shaped?run.characters.data():nullptr,run.shaped?run.glyphs.data():nullptr,
            run.shaped?run.advances.data():nullptr,run.shaped?run.offsets.data():nullptr,run.shaped?run.glyph_properties.data():nullptr,
            run.shaped?static_cast<int>(run.glyphs.size()):0);
        if(status) return status;
    }
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
