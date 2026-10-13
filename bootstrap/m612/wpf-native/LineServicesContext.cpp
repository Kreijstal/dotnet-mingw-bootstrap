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
#include <unicode/ubrk.h>
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
    bool shaped=false,hidden=false,tab=false;
    uint16_t leader=0;
};
struct Line {
    std::shared_ptr<Context> context;
    std::vector<Run> runs;
    LsLInfo info{};
    LsLineWidths widths{};
    int first=0,paragraph_first=0;
    Run terminator;
    bool has_terminator=false;
};
struct BreakRecord { int next,paragraph_first; std::shared_ptr<Context> context; };
std::mutex gate;
std::unordered_map<void*,std::shared_ptr<Context>> contexts;
std::unordered_map<void*,std::shared_ptr<Line>> lines;
std::unordered_map<void*,std::shared_ptr<BreakRecord>> records;

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
using UnderlineInfo=int (WINAPI *)(void*,uint32_t,LsHeights*,int,LsULInfo*);
using StrikeInfo=int (WINAPI *)(void*,uint32_t,LsHeights*,int,LsStInfo*);
using DrawDecoration=int (WINAPI *)(void*,uint32_t,uint32_t,LSPOINT*,int,int,int,uint32_t,LSRECT*);
using EnumTabCallback=int (WINAPI *)(void*,uint32_t,int,WCHAR*,uint16_t,int,int,int,LSPOINT*,LsHeights*,int);

int measure_run(Context& context,Run& run) {
    int count=static_cast<int>(run.text.size()),flow=run.flow,status=0;
    run.width=0; run.widths.assign(count,0);
    if(!run.hidden) {
        status=reinterpret_cast<Metrics>(context.info.pfnGetRunTextMetrics)(context.info.pols,
            static_cast<uint32_t>(run.id),0,flow,&run.metrics);
        if(status) return status;
        if(run.tab) return LS_None;
        if(run.properties.flags&0x400) {
            if(!context.callbacks.pfnGetGlyphsRedefined || !context.info.pfnGetGlyphPositions) return LS_NotImplemented;
            run.shaped=true; run.clusters.resize(count); run.characters.resize(count);
            std::vector<int> alone(count);
            int capacity=count+count/2+16,actual=0; bool generated=false;
            for(int retry=0;retry<3;++retry) {
                run.glyphs.resize(capacity); run.glyph_properties.resize(capacity);
                int used_glyphs=0;
                status=reinterpret_cast<Glyphs>(context.callbacks.pfnGetGlyphsRedefined)(context.info.pols,
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
            status=reinterpret_cast<Positions>(context.info.pfnGetGlyphPositions)(context.info.pols,&run.id,&count,1,0,
                run.text.data(),run.clusters.data(),run.characters.data(),count,run.glyphs.data(),run.glyph_properties.data(),actual,
                flow,run.advances.data(),run.offsets.data());
            if(status) return status;
            int64_t advance=0;
            for(int value:run.advances) advance+=value;
            if(advance<0 || advance>INT_MAX) return LS_InvalidFormatterResult;
            run.width=static_cast<int>(advance);
        } else {
            int measured=0,fitted=0;
            status=reinterpret_cast<Widths>(context.info.pfnGetRunCharWidths)(context.info.pols,
                static_cast<uint32_t>(run.id),0,run.text.data(),count,INT_MAX,flow,run.widths.data(),&measured,&fitted);
            if(status) return status;
            if(fitted!=count || measured<0) return LS_InvalidFormatterResult;
            run.width=measured;
        }
    }
    return LS_None;
}

int prefix_width(const Run& run,size_t end,int64_t& width) {
    if(run.hidden) return LS_None;
    if(!run.shaped) {
        for(size_t i=0;i<end;++i) width+=run.widths[i];
    } else {
        unsigned last=end==run.text.size()?run.advances.size():run.clusters[end];
        if(last>run.advances.size() || (end && end<run.text.size() && run.clusters[end]==run.clusters[end-1]))
            return LS_NotImplemented;
        for(unsigned i=0;i<last;++i) width+=run.advances[i];
    }
    return LS_None;
}

// Tab locations are relative to the paragraph origin. A break candidate
// aligns only its retained field, across client runs, rather than the rest
// of the paragraph that will move to the following line.
int tab_target(const Line& line,size_t index,int64_t x,int end_cp,int64_t& target,uint16_t& leader) {
    const Context& context=*line.context;
    leader=0;
    for(const auto& stop:context.tabs) {
        if(stop.ur<=x) continue;
        int64_t field=0;
        bool aligned=false;
        if(stop.lskt) {
            WCHAR align=stop.lskt==3?L'.':stop.wchCharTab;
            for(size_t j=index+1;j<line.runs.size() && !line.runs[j].tab;++j) {
                const auto& next=line.runs[j];
                if(next.cp>=end_cp) break;
                size_t end=std::min<size_t>(next.text.size(),end_cp-next.cp);
                if(stop.lskt>=3) {
                    auto found=std::find(next.text.begin(),next.text.begin()+end,align);
                    if(found!=next.text.begin()+end) { end=found-next.text.begin(); aligned=true; }
                }
                int status=prefix_width(next,end,field); if(status) return status;
                if(aligned) break;
            }
        }
        target=static_cast<int64_t>(stop.ur)-(stop.lskt==1?field/2:field);
        if(target<=x) continue;
        leader=stop.wchTabLeader; return LS_None;
    }
    if(context.incremental_tab<=0) return LS_InvalidFormatterResult;
    int64_t after=context.tabs.empty()?x:std::max<int64_t>(x,context.tabs.back().ur);
    target=(after/context.incremental_tab+1)*context.incremental_tab;
    return LS_None;
}

int layout_tabs(Line& line,int start,int& total) {
    int64_t x=start;
    for(size_t i=0;i<line.runs.size();++i) {
        auto& run=line.runs[i];
        if(run.tab && !run.hidden) {
            int64_t target=0;
            int status=tab_target(line,i,x,INT_MAX,target,run.leader); if(status) return status;
            if(target>x+INT_MAX || target>INT_MAX) return LS_InvalidFormatterResult;
            run.width=static_cast<int>(target-x); run.widths.assign(1,run.width);
        }
        x+=run.width;
        if(x<INT_MIN || x>INT_MAX) return LS_InvalidFormatterResult;
    }
    total=static_cast<int>(x); return LS_None;
}

int candidate_width(const Line& line,int count,int visible,int64_t& width) {
    int64_t x=line.widths.upStartMainText;
    int end_cp=line.first+count,visible_cp=line.first+visible;
    for(size_t i=0;i<line.runs.size();++i) {
        const auto& run=line.runs[i];
        if(run.cp>=visible_cp) break;
        if(run.tab && !run.hidden) {
            int64_t target=0; uint16_t leader=0;
            int status=tab_target(line,i,x,end_cp,target,leader); if(status) return status;
            x=target;
        } else {
            size_t end=std::min<size_t>(run.text.size(),visible_cp-run.cp);
            int status=prefix_width(run,end,x); if(status) return status;
        }
        if(x<INT_MIN || x>INT_MAX) return LS_InvalidFormatterResult;
    }
    width=x-line.widths.upStartMainText; return LS_None;
}

std::shared_ptr<Line> get_line(void* handle) {
    std::lock_guard<std::mutex> lock(gate);
    auto found=lines.find(handle); return found==lines.end()?nullptr:found->second;
}

int trailing_width(const Run& run,bool& all_spaces) {
    size_t end=run.text.size();
    while(end && (run.text[end-1]==L' ' || run.text[end-1]==0xA0)) --end;
    all_spaces=end==0;
    if(run.hidden) return 0;
    if(!run.shaped) {
        int width=0;
        for(size_t i=end;i<run.widths.size();++i) width+=run.widths[i];
        return width;
    }
    if(end==run.text.size()) return 0;
    // Only whole trailing clusters may be removed from the visible width.
    if(end && run.clusters[end]==run.clusters[end-1]) return 0;
    unsigned first=run.clusters[end];
    if(first>run.advances.size()) return 0;
    int width=0;
    for(size_t glyph=first;glyph<run.advances.size();++glyph) width+=run.advances[glyph];
    return width;
}

// Prefix widths are meaningful only at complete glyph-cluster boundaries.
int choose_wrap(const Line& line,int allowance,bool emergency,int max_cut,bool& unavoidable) {
    const auto& runs=line.runs;
    unavoidable=false;
    bool tabs=std::any_of(runs.begin(),runs.end(),[](const Run& run) { return run.tab && !run.hidden; });
    std::vector<UChar> text;
    std::vector<int64_t> width{0};
    std::vector<bool> boundary{true};
    for(const auto& run:runs) {
        int64_t before=width.back();
        text.insert(text.end(),run.text.begin(),run.text.end());
        if(!run.shaped || run.hidden) {
            for(size_t i=0;i<run.text.size();++i) {
                before+=run.hidden?0:run.widths[i]; width.push_back(before); boundary.push_back(true);
            }
        } else {
            for(size_t i=0;i<run.text.size();++i) { width.push_back(before); boundary.push_back(false); }
            size_t base=width.size()-run.text.size()-1;
            for(size_t i=0;i<run.text.size();) {
                size_t end=i+1;
                while(end<run.text.size() && run.clusters[end]==run.clusters[i]) ++end;
                unsigned first=run.clusters[i],last=end<run.text.size()?run.clusters[end]:run.advances.size();
                if(first>last || last>run.advances.size()) return -1;
                for(unsigned glyph=first;glyph<last;++glyph) before+=run.advances[glyph];
                width[base+end]=before; boundary[base+end]=true; i=end;
            }
        }
    }
    UErrorCode error=U_ZERO_ERROR;
    auto* iterator=ubrk_open(UBRK_LINE,"root",text.data(),static_cast<int>(text.size()),&error);
    if(U_FAILURE(error) || !iterator) return -1;
    int chosen=0,first_break=0;
    for(int point=ubrk_first(iterator);(point=ubrk_next(iterator))!=UBRK_DONE;) {
        if(point>max_cut || !boundary[point]) continue;
        if(!first_break) first_break=point;
        int visible=point;
        while(visible>0 && text[visible-1]==L' ') --visible;
        while(visible>0 && !boundary[visible]) --visible;
        int64_t actual=width[visible];
        if(tabs && candidate_width(line,point,visible,actual)) { ubrk_close(iterator); return -1; }
        if(actual<=allowance) chosen=point;
    }
    ubrk_close(iterator);
    if(chosen || !emergency) { unavoidable=!chosen; return chosen?chosen:first_break; }
    error=U_ZERO_ERROR;
    iterator=ubrk_open(UBRK_CHARACTER,"root",text.data(),static_cast<int>(text.size()),&error);
    if(U_FAILURE(error) || !iterator) return -1;
    first_break=0;
    for(int point=ubrk_first(iterator);(point=ubrk_next(iterator))!=UBRK_DONE;) {
        if(point>max_cut || !boundary[point]) continue;
        if(!first_break) first_break=point;
        int64_t actual=width[point];
        if(tabs && candidate_width(line,point,point,actual)) { ubrk_close(iterator); return -1; }
        if(actual<=allowance) chosen=point;
    }
    ubrk_close(iterator); unavoidable=!chosen; return chosen?chosen:first_break;
}

struct Cell {
    const Run* run;
    int cp,length,x,width,glyphs,run_x;
};

int text_cells(const Line& line,std::vector<Cell>& cells) {
    // A query cell names one client run. Until cross-run caret ownership is
    // implemented, reject graphemes crossing those runs rather than splitting
    // a user-perceived character at an artificial buffer boundary.
    if(line.runs.size()>1) {
        std::vector<UChar> text;
        for(const auto& run:line.runs) text.insert(text.end(),run.text.begin(),run.text.end());
        UErrorCode error=U_ZERO_ERROR;
        auto* raw=ubrk_open(UBRK_CHARACTER,"root",text.data(),static_cast<int>(text.size()),&error);
        if(U_FAILURE(error) || !raw) return LS_InvalidFormatterResult;
        std::unique_ptr<UBreakIterator,decltype(&ubrk_close)> iterator(raw,ubrk_close);
        int end=0;
        for(const auto& run:line.runs) {
            end+=static_cast<int>(run.text.size());
            if(!ubrk_isBoundary(raw,end)) return LS_NotImplemented;
        }
    }
    int x=line.widths.upStartMainText;
    for(const auto& run:line.runs) {
        if(run.flow!=0) return LS_NotImplemented;
        int run_x=x;
        UErrorCode error=U_ZERO_ERROR;
        auto* raw=ubrk_open(UBRK_CHARACTER,"root",reinterpret_cast<const UChar*>(run.text.data()),
            static_cast<int>(run.text.size()),&error);
        if(U_FAILURE(error) || !raw) return LS_InvalidFormatterResult;
        std::unique_ptr<UBreakIterator,decltype(&ubrk_close)> iterator(raw,ubrk_close);
        for(size_t start=0;start<run.text.size();) {
            size_t end=static_cast<size_t>(ubrk_following(raw,static_cast<int>(start)));
            if(end>run.text.size() || end<=start) return LS_InvalidFormatterResult;
            if(run.shaped) {
                while(end<run.text.size() && run.clusters[end]==run.clusters[end-1]) {
                    int next=ubrk_following(raw,static_cast<int>(end));
                    if(next==UBRK_DONE) { end=run.text.size(); break; }
                    end=static_cast<size_t>(next);
                }
            }
            int width=0,glyphs=0;
            if(!run.hidden) {
                if(run.shaped) {
                    unsigned first=run.clusters[start],last=end<run.text.size()?run.clusters[end]:run.advances.size();
                    if(first>last || last>run.advances.size()) return LS_NotImplemented;
                    glyphs=last-first;
                    for(unsigned glyph=first;glyph<last;++glyph) width+=run.advances[glyph];
                } else {
                    glyphs=static_cast<int>(end-start);
                    for(size_t i=start;i<end;++i) width+=run.widths[i];
                }
            }
            cells.push_back({&run,run.cp+static_cast<int>(start),static_cast<int>(end-start),x,width,glyphs,run_x});
            x+=width; start=end;
        }
    }
    if(line.has_terminator) cells.push_back({&line.terminator,line.terminator.cp,1,x,0,0,x});
    return LS_None;
}

void fill_query(const Line& line,const Cell& cell,LsQSubInfo& sub,LsTextCell& text) {
    const Run& run=*cell.run;
    sub={}; text={};
    sub.lstflowSubLine=run.flow; sub.lscpFirstSubLine=line.first;
    sub.lsdcpSubLine=line.info.cpLimToContinue-line.first;
    sub.pointUvStartSubLine={line.widths.upStartMainText,0};
    sub.lsHeightsPresSubLine={line.info.dvpAscent,line.info.dvpDescent,line.info.dvpMultiLineHeight};
    sub.dupSubLine=line.widths.upLimLine-line.widths.upStartMainText;
    sub.idobj=run.properties.idObj; sub.plsrun=reinterpret_cast<void*>(run.id);
    sub.lscpFirstRun=run.cp; sub.lsdcpRun=static_cast<int>(run.text.size());
    sub.pointUvStartRun={cell.run_x,0};
    sub.lsHeightsPresRun={run.metrics.dvAscent,run.metrics.dvDescent,run.metrics.dvMultiLineHeight};
    sub.dupRun=run.width;
    sub.pointUvStartObj=sub.pointUvStartRun; sub.lsHeightsPresObj=sub.lsHeightsPresRun; sub.dupObj=run.width;
    text.lscpStartCell=cell.cp; text.lscpEndCell=cell.cp+cell.length-1;
    text.pointUvStartCell={cell.x,0}; text.dupCell=cell.width;
    text.cCharsInCell=cell.length; text.cGlyphsInCell=cell.glyphs;
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
    if(flags&8) return LS_NotImplemented;
    try {
        auto result=std::make_shared<Line>(); result->context=context; result->first=cp; result->paragraph_first=cp;
        if(previous_break) {
            std::lock_guard<std::mutex> lock(gate);
            auto found=records.find(previous_break);
            if(found==records.end() || found->second->next!=cp || found->second->context!=context) return LS_InvalidBreakRecord;
            result->paragraph_first=found->second->paragraph_first;
        }
        LsPap paragraph{}; LsLineProps properties{};
        int status=reinterpret_cast<FetchPap>(context->info.pfnFetchPap)(context->info.pols,cp,&paragraph);
        if(status) return status;
        if(!previous_break) result->paragraph_first=paragraph.cpFirst;
        status=reinterpret_cast<FetchProps>(context->callbacks.pfnFetchLineProps)(context->info.pols,cp,cp==result->paragraph_first,&properties);
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
            run.hidden=hidden_flag!=0 || (run.properties.flags&0x10000)!=0;
            run.text.assign(text,text+count); run.widths.resize(count);
            if(count==1 && (text[0]==0x2028 || text[0]==0x2029)) {
                if(position==INT_MAX) return LS_InvalidFormatterResult;
                result->info.endr=text[0]==0x2029?2:4;
                run.hidden=true; result->terminator=std::move(run); result->has_terminator=true;
                ended=true; ++position; break;
            }
            if(limit>0 && position-cp+count>limit) count=limit-(position-cp);
            if(count<=0) { ended=true; break; }
            auto tab=std::find(text,text+count,L'\t');
            if(tab==text) { count=1; run.tab=true; }
            else if(tab!=text+count) count=static_cast<int>(tab-text);
            run.text.resize(count); run.widths.resize(count);
            status=measure_run(*context,run);
            if(status) return status;
            result->info.dvpAscent=std::max(result->info.dvpAscent,run.metrics.dvAscent);
            result->info.dvpDescent=std::max(result->info.dvpDescent,run.metrics.dvDescent);
            result->info.dvpMultiLineHeight=std::max(result->info.dvpMultiLineHeight,run.metrics.dvMultiLineHeight);
            if(static_cast<int64_t>(total)+run.width>INT_MAX || static_cast<int64_t>(position)+count>INT_MAX) return LS_InvalidFormatterResult;
            total+=run.width; position+=count;
            // Track trailing nominal spaces; shaped runs keep their full width
            // until cluster-aware whitespace trimming is implemented.
            trailing=total;
            if(!run.shaped) for(int i=count-1;i>=0 && (run.text[i]==L' '||run.text[i]==0xA0);--i) trailing-=run.widths[i];
            result->info.EffectsFlags|=run.properties.effectsFlags;
            result->runs.push_back(std::move(run));

            if(limit>0 && position-cp>=limit) ended=true;
        }
        if(!ended) return LS_TooLongParagraph;
        status=layout_tabs(*result,properties.durLeft,total); if(status) return status;
        // Fetch through the separator before finding Unicode breaks: a client
        // run boundary can be in the middle of a word or grapheme.
        if((paragraph.grpf&0x40) && total>column-properties.durRightBreak && limit==0) {
            int max_cut=INT_MAX;
            for(;;) {
                bool unavoidable=false;
                int cut=choose_wrap(*result,column-properties.durRightBreak-properties.durLeft,
                    (paragraph.grpf&0x80000)==0 || (flags&4),max_cut,unavoidable);
                if(cut<=0) return LS_InvalidFormatterResult;
                Line candidate=*result;
                int text_end=cp;
                for(const auto& item:candidate.runs) text_end+=static_cast<int>(item.text.size());
                bool truncated=cp+cut<text_end;
                int remaining=cut;
                size_t keep=0;
                for(auto& item:candidate.runs) {
                    int length=static_cast<int>(item.text.size());
                    if(remaining<=0) break;
                    if(remaining<length) {
                        item.text.resize(remaining);
                        status=measure_run(*context,item); if(status) return status;
                        length=remaining;
                    }
                    remaining-=length; ++keep;
                }
                candidate.runs.resize(keep);
                if(truncated) { candidate.info.endr=0; candidate.has_terminator=false; }
                status=layout_tabs(candidate,properties.durLeft,total); if(status) return status;
                int visible=total;
                for(auto item=candidate.runs.rbegin();item!=candidate.runs.rend();++item) {
                    bool all_spaces=false;
                    visible-=trailing_width(*item,all_spaces);
                    if(!all_spaces) break;
                }
                // Reshaping a retained prefix can change contextual advances.
                // Recheck the real retained geometry and try an earlier legal
                // candidate if it no longer fits. Only the first indivisible
                // unit/word may overflow, matching the selected wrapping policy.
                if(visible>column-properties.durRightBreak && !unavoidable) { max_cut=cut-1; continue; }
                *result=std::move(candidate);
                if(truncated) position=cp+cut;
                break;
            }
            total=properties.durLeft; trailing=total;
            result->info.dvpAscent=result->info.dvpDescent=result->info.dvpMultiLineHeight=0;
            result->info.EffectsFlags=0;
            for(const auto& item:result->runs) {
                total+=item.width;
                result->info.EffectsFlags|=item.properties.effectsFlags;
                result->info.dvpAscent=std::max(result->info.dvpAscent,item.metrics.dvAscent);
                result->info.dvpDescent=std::max(result->info.dvpDescent,item.metrics.dvDescent);
                result->info.dvpMultiLineHeight=std::max(result->info.dvpMultiLineHeight,item.metrics.dvMultiLineHeight);
            }
        }
        trailing=total;
        for(auto item=result->runs.rbegin();item!=result->runs.rend();++item) {
            bool all_spaces=false;
            trailing-=trailing_width(*item,all_spaces);
            if(!all_spaces) break;
        }
        result->info.dvrAscent=result->info.dvpAscent;
        result->info.dvrDescent=result->info.dvpDescent;
        result->info.dvrMultiLineHeight=result->info.dvpMultiLineHeight;
        if(result->runs.empty()) result->info.dvrMultiLineHeight=INT_MAX;
        result->info.cpLimToContinue=result->info.cpLimToStay=position;
        result->info.cpFirstVis=cp; result->info.fFirstLineInPara=cp==result->paragraph_first;
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

extern "C" __declspec(dllexport) int WINAPI LoAcquireBreakRecord(void* handle,void** output) {
    if(!output) return LS_NullOutputParameter; *output=nullptr;
    auto line=get_line(handle); if(!line) return LS_InvalidLine;
    try {
        auto record=std::make_shared<BreakRecord>(BreakRecord{line->info.cpLimToContinue,line->info.endr==2?line->info.cpLimToContinue:line->paragraph_first,line->context});
        void* key=record.get();
        { std::lock_guard<std::mutex> lock(gate); records.emplace(key,record); }
        *output=key; return LS_None;
    } catch(const std::bad_alloc&) { return LS_OutOfMemory; }
}
extern "C" __declspec(dllexport) int WINAPI LoCloneBreakRecord(void* handle,void** output) {
    if(!output) return LS_NullOutputParameter; *output=nullptr;
    try {
        std::lock_guard<std::mutex> lock(gate);
        auto found=records.find(handle); if(found==records.end()) return LS_InvalidBreakRecord;
        auto record=std::make_shared<BreakRecord>(*found->second);
        void* key=record.get(); records.emplace(key,record); *output=key; return LS_None;
    } catch(const std::bad_alloc&) { return LS_OutOfMemory; }
}
extern "C" __declspec(dllexport) int WINAPI LoDisposeBreakRecord(void* handle,BOOL) {
    std::lock_guard<std::mutex> lock(gate);
    auto found=records.find(handle); if(found==records.end()) return LS_InvalidBreakRecord;
    records.erase(found); return LS_None;
}

extern "C" __declspec(dllexport) int WINAPI LoQueryLineCpPpoint(void* handle,int cp,int capacity,
    LsQSubInfo* sub,int* depth,LsTextCell* text) {
    if(!depth || !text) return LS_NullOutputParameter;
    *depth=0; *text={};
    if(capacity<=0 || !sub) return LS_InvalidParameter;
    *sub={};
    auto line=get_line(handle); if(!line) return LS_InvalidLine;
    if(cp<line->first || cp>=line->info.cpLimToContinue) return LS_WrongGivenCp;
    try {
        std::vector<Cell> cells;
        int status=text_cells(*line,cells); if(status) return status;
        for(const auto& cell:cells) if(cp>=cell.cp && cp<cell.cp+cell.length) {
            fill_query(*line,cell,*sub,*text); *depth=1; return LS_None;
        }
        return LS_WrongGivenCp;
    } catch(const std::bad_alloc&) { return LS_OutOfMemory; }
}

extern "C" __declspec(dllexport) int WINAPI LoQueryLinePointPcp(void* handle,const LSPOINT* point,int capacity,
    LsQSubInfo* sub,int* depth,LsTextCell* text) {
    if(!depth || !text) return LS_NullOutputParameter;
    *depth=0; *text={};
    if(capacity<=0 || !sub) return LS_InvalidParameter;
    *sub={};
    if(!point) return LS_InvalidParameter;
    auto line=get_line(handle); if(!line) return LS_InvalidLine;
    try {
        std::vector<Cell> cells;
        int status=text_cells(*line,cells); if(status) return status;
        if(cells.empty()) return LS_WrongGivenCp;
        const Cell* chosen=nullptr;
        for(const auto& cell:cells) {
            if(cell.width<=0) continue;
            chosen=&cell;
            if(static_cast<int64_t>(point->x)<static_cast<int64_t>(cell.x)+cell.width) break;
        }
        if(!chosen) chosen=&cells.front();
        fill_query(*line,*chosen,*sub,*text); *depth=1; return LS_None;
    } catch(const std::bad_alloc&) { return LS_OutOfMemory; }
}

extern "C" __declspec(dllexport) int WINAPI LoDisplayLine(void* handle,LSPOINT* origin,uint32_t mode,LSRECT* clip) {
    if(!origin || !clip) return LS_InvalidParameter;
    auto line=get_line(handle); if(!line) return LS_InvalidLine;
    auto& context=*line->context;
    try {
    int x=origin->x+line->widths.upStartMainText;
    for(auto& run:line->runs) {
        LSPOINT point{x,origin->y};
        LsHeights height{run.metrics.dvAscent,run.metrics.dvDescent,run.metrics.dvMultiLineHeight};
        int status=0;
        if(!run.hidden) {
            if(run.tab) {
                if(run.leader && run.width) {
                    if(!context.info.pfnDrawTextRun) return LS_NotImplemented;
                    WCHAR leader=run.leader; int advance=0,measured=0,fitted=0;
                    status=reinterpret_cast<Widths>(context.info.pfnGetRunCharWidths)(context.info.pols,
                        static_cast<uint32_t>(run.id),0,&leader,1,INT_MAX,run.flow,&advance,&measured,&fitted);
                    if(status) return status;
                    if(fitted!=1 || advance<=0 || measured!=advance) return LS_InvalidFormatterResult;
                    int count=run.width/advance;
                    if(count>1048576) return LS_InvalidFormatterResult;
                    if(count) {
                        std::vector<WCHAR> text(count,leader);
                        std::vector<int> widths(count,advance);
                        int width=count*advance;
                        LSPOINT first{x+(run.width-width)/2,origin->y};
                        status=reinterpret_cast<DrawTextCallback>(context.info.pfnDrawTextRun)(context.info.pols,
                            static_cast<uint32_t>(run.id),&first,text.data(),widths.data(),count,run.flow,mode,&first,&height,width,clip);
                    }
                }
            } else if(run.shaped) {
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
            auto draw=[&](void* callback,uint32_t kind,int offset,int thickness) {
                int64_t y=static_cast<int64_t>(origin->y)+offset;
                if(thickness<=0 || y<INT_MIN || y>INT_MAX) return LS_InvalidFormatterResult;
                LSPOINT decoration{x,static_cast<int>(y)};
                return reinterpret_cast<DrawDecoration>(callback)(context.info.pols,static_cast<uint32_t>(run.id),kind,
                    &decoration,run.width,thickness,run.flow,mode,clip);
            };
            if(run.properties.flags&0x20000) {
                if(!context.info.pfnGetRunUnderlineInfo || !context.info.pfnDrawUnderline) return LS_NotImplemented;
                LsULInfo info{};
                status=reinterpret_cast<UnderlineInfo>(context.info.pfnGetRunUnderlineInfo)(context.info.pols,
                    static_cast<uint32_t>(run.id),&height,run.flow,&info);
                if(status) return status;
                if(info.cNumberOfLines<0 || info.cNumberOfLines>2) return LS_InvalidFormatterResult;
                int64_t offset=static_cast<int64_t>(info.dvpUnderlineOriginOffset)+info.dvpFirstUnderlineOffset;
                if(offset<INT_MIN || offset>INT_MAX) return LS_InvalidFormatterResult;
                if(info.cNumberOfLines) status=draw(context.info.pfnDrawUnderline,info.kulBase,static_cast<int>(offset),info.dvpFirstUnderlineSize);
                if(status) return status;
                offset+=static_cast<int64_t>(info.dvpFirstUnderlineSize)+info.dvpGapBetweenLines;
                if(info.cNumberOfLines==2) {
                    if(offset<INT_MIN || offset>INT_MAX) return LS_InvalidFormatterResult;
                    status=draw(context.info.pfnDrawUnderline,info.kulBase,static_cast<int>(offset),info.dvpSecondUnderlineSize);
                    if(status) return status;
                }
            }
            if(run.properties.flags&0x40000) {
                if(!context.info.pfnGetRunStrikethroughInfo || !context.info.pfnDrawStrikethrough) return LS_NotImplemented;
                LsStInfo info{};
                status=reinterpret_cast<StrikeInfo>(context.info.pfnGetRunStrikethroughInfo)(context.info.pols,
                    static_cast<uint32_t>(run.id),&height,run.flow,&info);
                if(status) return status;
                if(info.cNumberOfLines<0 || info.cNumberOfLines>2 || info.dvpLowerStrikethroughOffset==INT_MIN || info.dvpUpperStrikethroughOffset==INT_MIN)
                    return LS_InvalidFormatterResult;
                if(info.cNumberOfLines) status=draw(context.info.pfnDrawStrikethrough,info.kstBase,-info.dvpLowerStrikethroughOffset,info.dvpLowerStrikethroughSize);
                if(status) return status;
                if(info.cNumberOfLines==2) {
                    status=draw(context.info.pfnDrawStrikethrough,info.kstBase,-info.dvpUpperStrikethroughOffset,info.dvpUpperStrikethroughSize);
                    if(status) return status;
                }
            }
        }
        x+=run.width;
    }
    return LS_None;
    } catch(const std::bad_alloc&) { return LS_OutOfMemory; }
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
        if(run.tab) {
            if(!context.info.pfnEnumTab) return LS_NotImplemented;
            int status=reinterpret_cast<EnumTabCallback>(context.info.pfnEnumTab)(context.info.pols,
                static_cast<uint32_t>(run.id),run.cp,run.text.data(),run.leader,run.flow,
                reverse!=FALSE,geometry!=FALSE,&point,&height,run.width);
            if(status) return status;
            continue;
        }
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
