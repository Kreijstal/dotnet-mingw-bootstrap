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
#include <unicode/ubidi.h>
#include <unicode/uchar.h>
#include "LineServicesNative.h"
#include "ReverseObjectHandler.h"

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
    WpfReverseObjectHandler reverse_handler{};
};

struct GlyphOffset { int32_t du,dv; };
struct Run {
    int cp=0,flow=0,width=0,level=0,level_delta=0;
    uintptr_t id=0;
    LsChp properties{};
    LsTxM metrics{};
    std::vector<WCHAR> text;
    std::vector<int> widths,advances,nominal_advances;
    std::vector<uint16_t> glyphs,clusters,characters;
    std::vector<uint32_t> glyph_properties;
    std::vector<uint8_t> expansion;
    std::vector<GlyphOffset> offsets;
    bool shaped=false,hidden=false,tab=false,synthetic=false;
    uint16_t leader=0;
    bool embedded=false,position_used=false,hard_break=false;
    InlineInit object_handler{};
    ObjDim object_metrics{};
    int break_before=1,break_after=1;
};
struct BreakPenalty {
    int first=0,next=0,width=0,target=0;
    bool forced=false,hyphenated=false,terminal=false;
    double cost=0;
};
struct ParaSession;
struct Line {
    std::shared_ptr<Context> context;
    std::vector<Run> runs;
    std::vector<Run> markers;
    LsLInfo info{};
    LsLineWidths widths{};
    int first=0,paragraph_first=0,base_flow=0,initial_level=0;
    Run terminator;
    bool has_terminator=false;
    bool justified=false;
    int justification=0; // LsKJust.lskjFullInterWord
    std::shared_ptr<BreakPenalty> penalty;
    std::weak_ptr<ParaSession> penalty_session;
    bool detached_penalty=false;
};
struct ParaSession {
    std::shared_ptr<Context> context;
    int first=0,end=0,width=0;
    bool in_use=false;
    std::vector<LsLInfo> information;
    std::vector<void*> line_handles,penalty_handles;
    std::vector<std::shared_ptr<BreakPenalty>> detached;
};
struct BreakRecord { int next,paragraph_first; std::shared_ptr<Context> context; int level=0; };
// Source-owned state for paragraph penalty services. Scoring/optimal breaking
// will use this retained context; resource acquisition does not format text.
struct PenaltyState { std::shared_ptr<Context> context; };
struct PenaltyModule { std::shared_ptr<PenaltyState> state; };
std::mutex gate;
std::unordered_map<void*,std::shared_ptr<Context>> contexts;
std::unordered_map<void*,std::shared_ptr<Line>> lines;
std::unordered_map<void*,std::shared_ptr<BreakRecord>> records;
std::unordered_map<void*,std::shared_ptr<PenaltyModule>> penalties;
std::unordered_map<void*,std::shared_ptr<ParaSession>> sessions;
std::unordered_map<void*,std::weak_ptr<BreakPenalty>> break_penalties;
// A formatting scope retains this owner and prevents its destruction. Avoid
// registry locking for every control in optimal/geometry reconstruction; direct
// borrowed-handler calls outside that scope still validate the live registry.
static thread_local Context* reverse_active_context=nullptr;

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
using ObjectHandler=int (WINAPI *)(void*,uint32_t,InlineInit*);
using FormatObject=int (WINAPI *)(void*,uint32_t,int,int,int,ObjDim*,int*,int*,int*,int*);
using DrawObject=int (WINAPI *)(void*,uint32_t,LSPOINT*,int,int);
using CharExpansion=int(WINAPI*)(void*,int,int,LsCharRunInfo*,LsNeighborInfo*,LsNeighborInfo*,int,int**,int**);
using GlyphExpansion=int(WINAPI*)(void*,int,int,LsGlyphRunInfo*,LsNeighborInfo*,LsNeighborInfo*,int,int**,int**,uint8_t*,int*);
using HyphenOpportunity=int(WINAPI*)(void*,int,int,int*,int*,LsHyph*);
using AutoNumber=int(WINAPI*)(void*,int*,LsChp*,uintptr_t*,uint16_t*,LsChp*,uintptr_t*,int*,int*,int*);

int WINAPI reverse_transition(void* handle,int base,int level,int delta,int* output) {
    if(!output)return LS_NullOutputParameter;*output=0;
    if(!reverse_active_context||reverse_active_context!=handle){std::lock_guard<std::mutex> lock(gate);if(contexts.find(handle)==contexts.end())return LS_InvalidContext;}
    if((base!=0&&base!=1)||!delta)return LS_InvalidParameter;
    int64_t next=int64_t(level)+delta;
    if(level<base||level>125||next<base||next>125)return LS_InvalidFormatterResult;
    *output=int(next);return LS_None;
}
int transition_reverse(Context& context,Run& run,int base,int& level,int count,bool opening) {
    if(!context.reverse_handler.transition) {
        if(!context.info.pfnGetObjectHandlerInfo)return LS_NotImplemented;
        WpfReverseObjectHandler handler{};
        using Request=int(WINAPI*)(void*,uint32_t,void*);
        int status=reinterpret_cast<Request>(context.info.pfnGetObjectHandlerInfo)(context.info.pols,0,&handler);
        if(status)return status;
        if(handler.version||handler.kind!=WpfReverseObjectKind||handler.context!=context.info.pols||handler.transition!=reverse_transition)return LS_InvalidFormatterResult;
        context.reverse_handler=handler;
    }
    int delta=opening?count:-count,next=0;
    int status=context.reverse_handler.transition(context.reverse_handler.context,base,level,delta,&next);
    if(status)return status;
    run.level_delta=delta;level=next;return LS_None;
}

int format_object(Context& context,Run& run,int position,int margin) {
    if(!context.info.pfnGetObjectHandlerInfo) return LS_NotImplemented;
    int status=reinterpret_cast<ObjectHandler>(context.info.pfnGetObjectHandlerInfo)(context.info.pols,1,&run.object_handler);
    if(status) return status;
    if(run.object_handler.dwVersion || !run.object_handler.pfnFormat || !run.object_handler.pfnDraw) return LS_InvalidFormatterResult;
    int first=0,used=0;
    status=reinterpret_cast<FormatObject>(run.object_handler.pfnFormat)(context.info.pols,
        static_cast<uint32_t>(run.id),run.cp,position,margin,&run.object_metrics,&first,&used,&run.break_before,&run.break_after);
    if(status) return status;
    const auto& reference=run.object_metrics.heightsRef;
    const auto& height=run.object_metrics.heightsPres;
    auto valid=[](const LsHeights& value) {
        return value.dvAscent>=0 && value.dvDescent>=0 && value.dvMultiLineHeight>=0 &&
            static_cast<int64_t>(value.dvAscent)+value.dvDescent==value.dvMultiLineHeight;
    };
    if(run.object_metrics.dur<0 || !valid(reference) || !valid(height) ||
        run.break_before<0 || run.break_before>3 || run.break_after<0 || run.break_after>3 ||
        (first!=0 && first!=1) || (used!=0 && used!=1)) return LS_InvalidFormatterResult;
    run.width=run.object_metrics.dur; run.widths.assign(run.text.size(),0); run.widths[0]=run.width;
    run.metrics={height.dvAscent,height.dvDescent,height.dvMultiLineHeight,0};
    run.position_used=used!=0; return LS_None;
}

int measure_run(Context& context,Run& run) {
    if(run.embedded) return LS_None; // Objects are formatted atomically at their pen position.
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
            // GlyphRun and the original placement/expansion callbacks require a
            // zero-based, monotonic map entirely inside the actual glyph array.
            if(run.clusters.empty()||run.clusters[0]!=0)return LS_InvalidCharToGlyphMapping;
            for(size_t cp=0;cp<run.clusters.size();++cp)if(run.clusters[cp]>=actual||(cp&&run.clusters[cp]<run.clusters[cp-1]))return LS_InvalidCharToGlyphMapping;
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

int format_marker(Line& line,int requested,int& main_start) {
    auto& context=*line.context;
    if(!context.info.pfnGetAutoNumberInfo)return LS_NotImplemented;
    int alignment=0,word95=0,offset=0,width=0;uint16_t added=0;
    LsChp marker_props{},added_props{};uintptr_t marker_id=0,added_id=0;
    int status=reinterpret_cast<AutoNumber>(context.info.pfnGetAutoNumberInfo)(context.info.pols,&alignment,&marker_props,&marker_id,&added,&added_props,&added_id,&word95,&offset,&width);
    if(status)return status;
    if(alignment<0||alignment>2||word95||(added && added!=L'\t'))return LS_NotImplemented;
    if(added==L'\t') {
        auto stop=std::find_if(context.tabs.begin(),context.tabs.end(),[&](const LsTbd& tab){return tab.ur>requested;});
        if(stop==context.tabs.end()||stop->lskt)return LS_InvalidFormatterResult;
        main_start=stop->ur;
    }
    int position=LS_FirstMarkerCp,level=line.base_flow==4?1:0,total=0;
    bool ended=false;
    for(int attempts=0;attempts<9600&&!ended;++attempts) {
        Run run;run.cp=position;run.level=level;run.flow=(level&1)?4:0;
        std::vector<WCHAR> buffer(4096);WCHAR* text=nullptr;int used=0,count=0,hidden=0;
        for(int retry=0;retry<3;++retry) {
            status=reinterpret_cast<FetchRun>(context.callbacks.pfnFetchRunRedefined)(context.info.pols,position,0,nullptr,buffer.data(),int(buffer.size()),&used,&text,&count,&hidden,&run.properties,&run.id);
            if(status)return status;
            if(count<=0||count>9600)return LS_InvalidDcpFetched;
            if(used){text=buffer.data();break;}
            if(text)break;buffer.resize(count);
        }
        if(!text)return LS_InvalidCpContentFetched;
        if(int64_t(position)+count>=0)return LS_InvalidAutonumRun;
        bool opening=run.properties.idObj==0;
        bool closing=run.properties.idObj==0xFFFF && text[0]==0xFFFB && (run.id&0x0FFFFFFFu)==0;
        if(opening||closing) {
            if(opening&&text[0]!=0xFFFC)return LS_InvalidFormatterResult;
            status=transition_reverse(context,run,line.base_flow==4?1:0,level,count,opening);if(status)return status;
            run.hidden=true;run.text.assign(count,opening?0xFFFC:0xFFFB);run.widths.assign(count,0);
            line.markers.push_back(std::move(run));position+=count;continue;
        }
        if(count==1 && (text[0]==0x2028||text[0]==0x2029)){ended=true;break;}
        if(run.properties.idObj!=0xFFFF)return LS_NotImplemented;
        run.hidden=hidden!=0||(run.properties.flags&0x10000)!=0;
        run.text.assign(text,text+count);status=measure_run(context,run);if(status)return status;
        if(int64_t(total)+run.width>INT_MAX)return LS_InvalidFormatterResult;
        total+=run.width;position+=count;line.markers.push_back(std::move(run));
    }
    if(!ended)return LS_TooLongParagraph;
    int64_t start=requested-(alignment==2?int64_t(total):alignment==1?total/2:0);
    if(start<INT_MIN||start>INT_MAX||start+total>INT_MAX)return LS_InvalidFormatterResult;
    line.widths.upStartMarker=int(start);line.widths.upLimMarker=int(start)+total;
    return LS_None;
}

int measure_hyphen(Context& context,const Run& owner,int cp,Run& marker) {
    marker=owner;marker.cp=cp;
    marker.text.assign(1,context.info.wchHyphen?context.info.wchHyphen:L'-');
    marker.synthetic=true;marker.level_delta=0;marker.shaped=false;
    marker.glyphs.clear();marker.clusters.clear();marker.characters.clear();
    marker.advances.clear();marker.nominal_advances.clear();marker.offsets.clear();
    marker.glyph_properties.clear();marker.expansion.clear();
    return measure_run(context,marker);
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
        if(run.hard_break) { x=start; continue; }
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
    if(run.hidden){all_spaces=true;return 0;}
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

LsNeighborInfo neighbor(const Run* run,bool last) {
    LsNeighborInfo value{};
    if(!run || run->text.empty()) return value;
    value.fNeighborIsPresent=1; value.plsrun=static_cast<uint32_t>(run->id);
    value.fNeighborIsText=!run->hidden && !run->embedded && !run->tab;
    size_t index=last?run->text.size()-1:0;
    value.wch=run->text[index]; value.fGlyphBased=run->shaped;
    if(run->shaped) {
        unsigned glyph=run->clusters[index];
        if(glyph<run->glyphs.size()) { value.chprop=run->characters[index]; value.gindex=run->glyphs[glyph]; value.gprop=run->glyph_properties[glyph]; }
    }
    return value;
}

struct SourceGraphemes {
    std::vector<UChar> text;std::vector<int> logical{0};int first=0;
    std::unique_ptr<UBreakIterator,decltype(&ubrk_close)> breaks{nullptr,ubrk_close};
    int initialize(const Line& line) {
        first=line.first;
        // Reversal controls belong to the LS transport, not the Unicode source.
        for(const auto& run:line.runs)if(!run.synthetic)for(WCHAR ch:run.text){if(!run.level_delta)text.push_back(ch);logical.push_back(int(text.size()));}
        UErrorCode error=U_ZERO_ERROR;
        breaks.reset(ubrk_open(UBRK_CHARACTER,"root",text.data(),int(text.size()),&error));
        return U_FAILURE(error)||!breaks?LS_InvalidFormatterResult:LS_None;
    }
    bool boundary(int cp) const {return cp>=first&&size_t(cp-first)<logical.size()&&ubrk_isBoundary(breaks.get(),logical[cp-first]);}
};

int justify_line(Line& line,int target,int& total,int& trailing) {
    int64_t remaining=static_cast<int64_t>(target)-trailing;
    if(remaining<=0) return LS_None;
    auto& context=*line.context;bool mixed=line.justification==4; // LsKJust.lskjFullMixed
    SourceGraphemes graphemes;if(mixed){int status=graphemes.initialize(line);if(status)return status;}
    if(std::any_of(line.runs.begin(),line.runs.end(),[](const Run& run) { return run.tab; }) &&
        std::any_of(context.tabs.begin(),context.tabs.end(),[](const LsTbd& stop) { return stop.lskt!=0; })) return LS_NotImplemented;
    int visible_cp=line.first;
    for(const auto& run:line.runs)if(!run.synthetic)visible_cp+=static_cast<int>(run.text.size());
    for(auto run=line.runs.rbegin();run!=line.runs.rend();++run) {
        if(run->level_delta){visible_cp-=int(run->text.size());continue;}
        size_t count=run->text.size();
        while(count && (run->text[count-1]==L' ' || run->text[count-1]==0xa0)) { --count; --visible_cp; }
        if(count) break;
    }
    struct Opportunity { Run* run; size_t index; int capacity[3]; int added; bool left; uint8_t type; };
    std::vector<Opportunity> sites;
    size_t begin=0;
    for(size_t i=0;i<line.runs.size();++i) if(line.runs[i].tab) begin=i+1;
    for(size_t index=begin;index<line.runs.size();++index) {
        auto& run=line.runs[index];
        if(run.synthetic || run.hidden || run.embedded || run.tab || run.cp>=visible_cp) continue;
        size_t count=run.shaped?run.advances.size():run.text.size();
        std::vector<int> left[3],right[3]; int* lp[3]; int* rp[3];
        for(int level=0;level<3;++level) { left[level].assign(count,0);right[level].assign(count,0);lp[level]=left[level].data();rp[level]=right[level].data(); }
        std::vector<uint8_t> types(count,1);std::vector<int> ink(count),zero(run.text.size());
        auto previous=neighbor(index?&line.runs[index-1]:nullptr,true);
        auto next=neighbor(index+1<line.runs.size()?&line.runs[index+1]:nullptr,false);
        int status=0;
        if(run.shaped) {
            if(!context.info.pfnGetGlyphExpansionInfoFullMixed) return LS_NotImplemented;
            LsGlyphRunInfo info{static_cast<uint32_t>(run.id),reinterpret_cast<uint16_t*>(run.text.data()),run.clusters.data(),run.characters.data(),
                static_cast<int>(run.text.size()),0,run.glyphs.data(),run.glyph_properties.data(),run.advances.data(),run.offsets.data(),static_cast<int>(count)};
            status=reinterpret_cast<GlyphExpansion>(context.info.pfnGetGlyphExpansionInfoFullMixed)(context.info.pols,0,run.flow,&info,&previous,&next,3,lp,rp,types.data(),ink.data());
        } else {
            if(!context.info.pfnGetCharExpansionInfoFullMixed) return LS_NotImplemented;
            LsCharRunInfo info{static_cast<uint32_t>(run.id),reinterpret_cast<uint16_t*>(run.text.data()),run.widths.data(),zero.data(),zero.data(),static_cast<int>(count)};
            status=reinterpret_cast<CharExpansion>(context.info.pfnGetCharExpansionInfoFullMixed)(context.info.pols,0,run.flow,&info,&previous,&next,3,lp,rp);
        }
        if(status) return status;
        for(size_t item=0;item<count;++item) {
            if(types[item]>3 || ink[item]<0) return LS_InvalidFormatterResult;
            for(int level=0;level<3;++level) if(left[level][item]<0 || right[level][item]<0) return LS_InvalidFormatterResult;
        }
        for(size_t cp=0;cp<run.text.size() && run.cp+static_cast<int>(cp)<visible_cp;++cp) {
            size_t end=cp+1,item=cp;
            if(run.shaped) {
                if(cp&&run.clusters[cp]==run.clusters[cp-1])continue;
                while(end<run.text.size()&&run.clusters[end]==run.clusters[cp])++end;
                unsigned glyph_end=end<run.text.size()?run.clusters[end]:run.advances.size();
                if(glyph_end<=run.clusters[cp]||glyph_end>run.advances.size())return LS_InvalidCharToGlyphMapping;
                item=glyph_end-1;
            }
            bool space=end==cp+1&&u_isUWhiteSpace(run.text[cp]);
            if(!space&&(!mixed||run.cp+int(end)>=visible_cp||!graphemes.boundary(run.cp+int(end))))continue;
            if(run.shaped){if(types[item]==0)continue;if(types[item]!=1)return LS_NotImplemented;}
            for(int side=0;side<2;++side) {
                auto* capacities=side?right:left;
                sites.push_back({&run,item,{capacities[0][item],capacities[1][item],capacities[2][item]},0,side==0,types[item]});
            }
        }
    }
    int64_t added=0;
    for(int level=0;level<3 && remaining>0;++level) {
        for(;;) {
            size_t active=0;
            for(const auto& site:sites) if(site.capacity[level]>site.added) ++active;
            if(!active || !remaining) break;
            int64_t share=(remaining+active-1)/active;
            for(auto& site:sites) {
                int64_t room=static_cast<int64_t>(site.capacity[level])-site.added;
                if(room<=0) continue;
                int amount=static_cast<int>(std::min({room,share,remaining}));
                site.added+=amount;remaining-=amount;added+=amount;
            }
        }
    }
    if(static_cast<int64_t>(total)+added>INT_MAX) return LS_InvalidFormatterResult;
    for(const auto& site:sites) if(site.added) {
        auto& run=*site.run;
        if(run.shaped && run.nominal_advances.empty()) run.nominal_advances=run.advances;
        int& advance=run.shaped?run.advances[site.index]:run.widths[site.index];
        if(static_cast<int64_t>(advance)+site.added>INT_MAX || static_cast<int64_t>(run.width)+site.added>INT_MAX) return LS_InvalidFormatterResult;
        advance+=site.added;run.width+=site.added;
        if(run.shaped) {
            run.expansion[site.index]=site.type;
            if(site.left) {
                if(static_cast<int64_t>(run.offsets[site.index].du)+site.added>INT_MAX) return LS_InvalidFormatterResult;
                run.offsets[site.index].du+=site.added;
            }
        }
    }
    total+=static_cast<int>(added);trailing+=static_cast<int>(added);return LS_None;
}

struct BreakGeometry {
    std::vector<UChar> text;
    std::vector<int64_t> width{0};
    std::vector<int> raw{0};
    std::vector<int> characters;
    std::vector<bool> boundary{true};
    std::vector<int> before,after;
    bool allowed(int point,bool natural) const {
        int left=after[point],right=before[point];
        if(left<0 && right<0) return natural;
        if(left<0) left=natural?2:1;
        if(right<0) right=point==static_cast<int>(text.size())?3:natural?2:1;
        return left==3 || right==3 || ((left!=0 && right!=0) && (left==2 || right==2));
    }
};

int break_geometry(const Line& line,BreakGeometry& geometry) {
    const auto& runs=line.runs;
    auto& text=geometry.text;auto& width=geometry.width;auto& boundary=geometry.boundary;
    int consumed=0;
    for(const auto& run:runs) {
        if(run.level_delta){
            consumed+=int(run.text.size());
            // WPF maps a source boundary before an opening reverse but after
            // closing reverses. Openings therefore belong to the next line.
            if(run.level_delta<0)geometry.raw.back()=consumed;
            continue;
        }
        for(size_t i=0;i<run.text.size();++i){geometry.characters.push_back(run.cp+int(i));geometry.raw.push_back(++consumed);}
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
                if(first>last || last>run.advances.size()) return LS_InvalidCharToGlyphMapping;
                for(unsigned glyph=first;glyph<last;++glyph) before+=run.advances[glyph];
                width[base+end]=before; boundary[base+end]=true; i=end;
            }
        }
    }
    geometry.before.assign(text.size()+1,-1);geometry.after.assign(text.size()+1,-1);
    auto& before=geometry.before;auto& after=geometry.after;
    for(const auto& run:runs) if(run.embedded) {
        size_t at=std::lower_bound(geometry.raw.begin(),geometry.raw.end(),run.cp-line.first)-geometry.raw.begin();
        if(at>=before.size()||at+run.text.size()>=after.size())return LS_InvalidFormatterResult;
        before[at]=run.break_before;after[at+run.text.size()]=run.break_after;
    }
    return LS_None;
}

// Prefix widths are meaningful only at complete glyph-cluster boundaries.
int choose_wrap(const Line& line,int allowance,bool emergency,int max_cut,bool& unavoidable) {
    unavoidable=false;
    bool tabs=std::any_of(line.runs.begin(),line.runs.end(),[](const Run& run) { return run.tab && !run.hidden; });
    BreakGeometry geometry;if(break_geometry(line,geometry)) return -1;
    const auto& text=geometry.text;const auto& width=geometry.width;const auto& boundary=geometry.boundary;
    auto allowed=[&](int point,bool natural) { return geometry.allowed(point,natural); };
    UErrorCode error=U_ZERO_ERROR;
    auto* iterator=ubrk_open(UBRK_LINE,"root",text.data(),static_cast<int>(text.size()),&error);
    if(U_FAILURE(error) || !iterator) return -1;
    int chosen=0,first_break=0;
    for(int point=1;point<=static_cast<int>(text.size()) && geometry.raw[point]<=max_cut;++point) {
        if(!boundary[point] || !allowed(point,ubrk_isBoundary(iterator,point))) continue;
        int visible=point;
        while(visible>0 && text[visible-1]==L' ') --visible;
        while(visible>0 && !boundary[visible]) --visible;
        int64_t actual=width[visible];
        if(tabs) {
            int status=candidate_width(line,geometry.raw[point],geometry.raw[visible],actual);
            if(status==LS_InvalidFormatterResult) continue; // No usable stop for this retained field.
            if(status) { ubrk_close(iterator); return -1; }
        }
        if(!first_break) first_break=point;
        if(actual<=allowance) chosen=point;
    }
    ubrk_close(iterator);
    if(chosen||!emergency){unavoidable=!chosen;int point=chosen?chosen:first_break;return point>0?geometry.raw[point]:point;}
    error=U_ZERO_ERROR;
    iterator=ubrk_open(UBRK_CHARACTER,"root",text.data(),static_cast<int>(text.size()),&error);
    if(U_FAILURE(error) || !iterator) return -1;
    first_break=0;
    for(int point=ubrk_first(iterator);(point=ubrk_next(iterator))!=UBRK_DONE;) {
        if(geometry.raw[point]>max_cut || !boundary[point] || !allowed(point,true)) continue;
        int64_t actual=width[point];
        if(tabs) {
            int status=candidate_width(line,geometry.raw[point],geometry.raw[point],actual);
            if(status==LS_InvalidFormatterResult) continue;
            if(status) { ubrk_close(iterator); return -1; }
        }
        if(!first_break) first_break=point;
        if(actual<=allowance) chosen=point;
    }
    ubrk_close(iterator);unavoidable=!chosen;int point=chosen?chosen:first_break;return point>0?geometry.raw[point]:point;
}

int hyphen_opportunity(const Line& line,int allowance,int max_cut,int natural,bool natural_overflow,int& cut,Run& marker) {
    cut=0;auto& context=*line.context;if(!context.info.pfnGetNextHyphenOpp)return LS_None;
    BreakGeometry geometry;int status=break_geometry(line,geometry);if(status)return status;
    UErrorCode error=U_ZERO_ERROR;std::unique_ptr<UBreakIterator,decltype(&ubrk_close)> words(ubrk_open(UBRK_WORD,"root",geometry.text.data(),int(geometry.text.size()),&error),ubrk_close);
    if(U_FAILURE(error)||!words)return LS_InvalidFormatterResult;
    int start=ubrk_first(words.get()),end=0;
    while((end=ubrk_next(words.get()))!=UBRK_DONE) {
        if(ubrk_getRuleStatus(words.get())<UBRK_WORD_LETTER||ubrk_getRuleStatus(words.get())>=UBRK_WORD_KANA){start=end;continue;}
        // A break boundary can precede a zero-source-length opening reverse;
        // lexical callbacks instead need the CP of the actual text character.
        int first=geometry.characters[start],last=geometry.characters[end-1];
        for(int current=first;current<last;) {
            int found=0,hyphen=0;LsHyph detail{};
            status=reinterpret_cast<HyphenOpportunity>(context.info.pfnGetNextHyphenOpp)(context.info.pols,current,last-current,&found,&hyphen,&detail);if(status)return status;
            if(!found)break;if(found!=1||hyphen<current||hyphen>=last)return LS_WrongHyphenationPosition;
            current=hyphen+1;if(detail.kysr!=0)return LS_NotImplemented;
            int raw=current-line.first;auto at=std::lower_bound(geometry.raw.begin(),geometry.raw.end(),raw);
            if(at==geometry.raw.end())continue;size_t index=size_t(at-geometry.raw.begin());raw=*at;
            if(raw>max_cut||!geometry.boundary[index]||raw<=cut||(!natural_overflow&&raw<=natural))continue;
            auto owner=std::find_if(line.runs.begin(),line.runs.end(),[&](const Run& run){return !run.hidden&&!run.embedded&&!run.tab&&hyphen>=run.cp&&hyphen<run.cp+int(run.text.size());});if(owner==line.runs.end())continue;
            Run next;status=measure_hyphen(context,*owner,hyphen,next);if(status)return status;
            if(geometry.width[index]+next.width<=allowance){cut=raw;marker=std::move(next);}
        }
        start=end;
    }
    return LS_None;
}

int range_width(const Line& line,int first,int end,int visible,int64_t& width) {
    int64_t x=line.widths.upStartMainText;
    for(size_t i=0;i<line.runs.size();++i) {
        const auto& run=line.runs[i];
        int run_end=run.cp+static_cast<int>(run.text.size());
        if(run_end<=first) continue;
        if(run.cp>=visible) break;
        if(run.tab && !run.hidden) {
            int64_t target=0;uint16_t leader=0;
            int status=tab_target(line,i,x,end,target,leader);if(status)return status;
            x=target;
        } else {
            size_t begin=first>run.cp?first-run.cp:0;
            size_t finish=std::min<size_t>(run.text.size(),visible-run.cp);
            int64_t before=0,after=0;
            int status=prefix_width(run,begin,before);if(status)return status;
            status=prefix_width(run,finish,after);if(status)return status;
            x+=after-before;
        }
        if(x<INT_MIN || x>INT_MAX)return LS_InvalidFormatterResult;
    }
    width=x;return LS_None;
}

int paragraph_widths(const Line& line,int& maximum_total,int& maximum_visible,int& minimum_total,int& minimum_visible) {
    BreakGeometry geometry;int status=break_geometry(line,geometry);if(status)return status;
    const auto& text=geometry.text;
    UErrorCode error=U_ZERO_ERROR;
    auto* raw=ubrk_open(UBRK_LINE,"root",text.data(),static_cast<int>(text.size()),&error);
    if(U_FAILURE(error) || !raw)return LS_InvalidFormatterResult;
    std::unique_ptr<UBreakIterator,decltype(&ubrk_close)> iterator(raw,ubrk_close);
    maximum_total=maximum_visible=minimum_total=minimum_visible=line.widths.upStartMainText;
    int segment=0,hard_first=0;
    auto measure=[&](int first,int end,int& total,int& visible) {
        int trim=end;
        while(trim>first && (text[trim-1]==L' ' || text[trim-1]==0xa0 || text[trim-1]==0x2028))--trim;
        while(trim>first && !geometry.boundary[trim])--trim;
        int64_t full=0,shown=0;
        int result=range_width(line,line.first+geometry.raw[first],line.first+geometry.raw[end],line.first+geometry.raw[end],full);if(result)return result;
        result=range_width(line,line.first+geometry.raw[first],line.first+geometry.raw[end],line.first+geometry.raw[trim],shown);if(result)return result;
        total=std::max(total,static_cast<int>(full));visible=std::max(visible,static_cast<int>(shown));return LS_None;
    };
    for(int point=1;point<=static_cast<int>(text.size());++point) {
        bool hard=text[point-1]==0x2028;
        if(hard || (geometry.boundary[point] && geometry.allowed(point,ubrk_isBoundary(raw,point)))) {
            status=measure(segment,point,minimum_total,minimum_visible);if(status)return status;
            segment=point;
        }
        if(hard || point==static_cast<int>(text.size())) {
            status=measure(hard_first,point,maximum_total,maximum_visible);if(status)return status;
            hard_first=point;
        }
    }
    if(segment<static_cast<int>(text.size())) { status=measure(segment,text.size(),minimum_total,minimum_visible);if(status)return status; }
    return LS_None;
}

int reset_line_whitespace(Line& line) {
    // UAX #9 L1 is a line operation: wrapping can leave whitespace inside
    // a directional run. It must move to the paragraph's visual line end.
    int base=line.base_flow==4?1:0;
    for(size_t index=line.runs.size();index>0;) {
        --index;auto& run=line.runs[index];
        if(run.synthetic||run.embedded)break;
        if(run.hidden){run.level=base;run.flow=line.base_flow;continue;}
        size_t first=run.text.size();
        while(first){auto kind=u_charDirection(run.text[first-1]);if(kind!=U_WHITE_SPACE_NEUTRAL&&kind!=U_SEGMENT_SEPARATOR&&kind!=U_BLOCK_SEPARATOR)break;--first;}
        if(first==run.text.size())break;
        if(run.level!=base) {
            if(first) {
                Run tail=run;tail.cp+=int(first);tail.text.erase(tail.text.begin(),tail.text.begin()+first);tail.level=base;tail.flow=line.base_flow;tail.level_delta=0;
                run.text.resize(first);int status=measure_run(*line.context,run);if(status)return status;
                status=measure_run(*line.context,tail);if(status)return status;
                line.runs.insert(line.runs.begin()+index+1,std::move(tail));
            } else {run.level=base;run.flow=line.base_flow;}
        }
        if(first)break;
    }
    return LS_None;
}

struct RunGeometry { std::vector<int> left,edge;std::vector<int32_t> order; };
RunGeometry visual_runs(const Line& line) {
    RunGeometry result;size_t count=line.runs.size();result.left.resize(count);result.edge.resize(count);result.order.resize(count);
    std::vector<UBiDiLevel> levels;for(const auto& run:line.runs)levels.push_back(static_cast<UBiDiLevel>(run.level));
    if(count)ubidi_reorderVisual(levels.data(),int32_t(count),result.order.data());
    if(line.base_flow==4)std::reverse(result.order.begin(),result.order.end());
    int pen=line.widths.upStartMainText;
    for(int32_t index:result.order){const auto& run=line.runs[index];result.left[index]=pen;result.edge[index]=pen+(run.flow!=line.base_flow?run.width:0);pen+=run.width;}
    return result;
}

struct Cell {
    const Run* run;
    int cp,length,x,width,glyphs,run_x;
    int flow=0,level=0;bool joined=false;
};

int text_cells(const Line& line,std::vector<Cell>& cells) {
    // Unicode text elements are properties of the complete retained line,
    // including when font/style/client buffers divide one grapheme.
    SourceGraphemes graphemes;int status=graphemes.initialize(line);if(status)return status;
    auto geometry=visual_runs(line);int x=line.widths.upStartMainText;
    for(size_t index=0;index<line.runs.size();++index) {
        const auto& run=line.runs[index];if(run.synthetic)continue;x=geometry.edge[index];int sign=run.flow==line.base_flow?1:-1;
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
                    glyphs=run.embedded?0:static_cast<int>(end-start);
                    for(size_t i=start;i<end;++i) width+=run.widths[i];
                }
            }
            cells.push_back({&run,run.cp+static_cast<int>(start),static_cast<int>(end-start),x,width,glyphs,run_x});
            x+=sign*width; start=end;
        }
    }
    std::vector<Cell> joined;
    for(size_t index=0;index<cells.size();++index) {
        Cell cell=cells[index];cell.flow=cell.run->flow;cell.level=cell.run->level;
        int left=cell.x-(cell.flow!=line.base_flow?cell.width:0),right=left+cell.width;
        while(!graphemes.boundary(cell.cp+cell.length)) {
            if(index+1>=cells.size())return LS_InvalidFormatterResult;
            const auto& next=cells[++index];
            if(next.cp!=cell.cp+cell.length)return LS_InvalidFormatterResult;
            int next_left=next.x-(next.run->flow!=line.base_flow?next.width:0);
            if(next.width){left=std::min(left,next_left);right=std::max(right,next_left+next.width);}
            if(int64_t(cell.width)+next.width>INT_MAX||int64_t(cell.glyphs)+next.glyphs>INT_MAX)return LS_InvalidFormatterResult;
            cell.length+=next.length;cell.width+=next.width;cell.glyphs+=next.glyphs;cell.joined=true;
        }
        // A single cell can contain internal reversal controls and style runs,
        // but must occupy one contiguous visual interval without foreign ink.
        if(int64_t(right)-left!=cell.width)return LS_NotImplemented;
        cell.x=cell.flow==line.base_flow?left:right;
        joined.push_back(cell);
    }
    cells.swap(joined);
    for(const auto& run:line.runs)if(run.synthetic){auto at=std::find_if(cells.begin(),cells.end(),[&](const Cell& cell){return run.cp>=cell.cp&&run.cp<cell.cp+cell.length;});if(at!=cells.end())at->width+=run.width;}
    std::sort(cells.begin(),cells.end(),[&](const Cell& a,const Cell& b){int ax=a.x-(a.flow!=line.base_flow?a.width:0),bx=b.x-(b.flow!=line.base_flow?b.width:0);return ax<bx;});
    x=line.widths.upLimLine;
    if(line.has_terminator) cells.push_back({&line.terminator,line.terminator.cp,1,x,0,0,x,line.terminator.flow,line.terminator.level});
    return LS_None;
}

void fill_query(const Line& line,const Cell& cell,LsQSubInfo& sub,LsTextCell& text) {
    const Run& run=*cell.run;
    sub={}; text={};
    sub.lstflowSubLine=cell.flow; sub.lscpFirstSubLine=line.first;
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

int query_cell(const Line& line,const Cell& source,int capacity,LsQSubInfo* sub,int* depth,LsTextCell* text,int cp=-1) {
    Cell cell=source;auto geometry=visual_runs(line);
    // A complete text cell may cross style runs; the subline record must still
    // identify the actual source run containing a requested code point.
    if(cp>=0)for(size_t at=0;at<line.runs.size();++at){const auto& run=line.runs[at];if(!run.synthetic&&cp>=run.cp&&cp<run.cp+int(run.text.size())){cell.run=&run;cell.run_x=geometry.edge[at];break;}}
    int base=line.base_flow==4?1:0,needed=std::max(1,cell.level-base+1);
    if(capacity<needed)return LS_InvalidParameter;
    fill_query(line,cell,sub[needed-1],*text);
    size_t index=0;while(index<line.runs.size()&& &line.runs[index]!=cell.run)++index;
    for(int offset=0;offset<needed;++offset){
        auto value=sub[needed-1];value.lstflowSubLine=((base+offset)&1)?4:0;
        if(offset==0){value.lscpFirstSubLine=line.first;value.lsdcpSubLine=line.info.cpLimToContinue-line.first;value.pointUvStartSubLine={line.widths.upStartMainText,0};value.dupSubLine=line.widths.upLimLine-line.widths.upStartMainText;}
        else if(cell.joined) {
            // Internal client reversal scopes do not divide one source text
            // element. Every CP in the cell observes the same caret subline.
            value.lscpFirstSubLine=cell.cp;value.lsdcpSubLine=cell.length;
            value.pointUvStartSubLine={cell.x,0};value.dupSubLine=cell.width;
        } else {
            size_t first=index,last=index;int level=base+offset;
            while(first&&line.runs[first-1].level>=level)--first;
            while(last+1<line.runs.size()&&line.runs[last+1].level>=level)++last;
            int left=INT_MAX,right=INT_MIN;int cp_first=INT_MAX,cp_last=INT_MIN;
            for(size_t at=first;at<=last;++at){const auto& run=line.runs[at];if(run.level_delta)continue;left=std::min(left,geometry.left[at]);right=std::max(right,geometry.left[at]+run.width);cp_first=std::min(cp_first,run.cp);cp_last=std::max(cp_last,run.cp+int(run.text.size()));}
            if(left==INT_MAX){left=right=cell.run_x;cp_first=cell.cp;cp_last=cell.cp+cell.length;}
            value.lscpFirstSubLine=cp_first;value.lsdcpSubLine=cp_last-cp_first;value.pointUvStartSubLine={value.lstflowSubLine==line.base_flow?left:right,0};value.dupSubLine=right-left;
        }
        sub[offset]=value;
    }
    *depth=needed;return LS_None;
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

extern "C" __declspec(dllexport) int WINAPI LocbkGetObjectHandlerInfo(void* handle,uint32_t object_id,void* output) {
    if(!output)return LS_NullOutputParameter;
    // Only object zero uses this private record. Do not write an unknown
    // object's output buffer, whose size/protocol is not ours to assume.
    if(object_id!=0)return LS_NotImplemented;
    auto handler=static_cast<WpfReverseObjectHandler*>(output);*handler={};
    {std::lock_guard<std::mutex> lock(gate);if(contexts.find(handle)==contexts.end())return LS_InvalidContext;}
    *handler={0,WpfReverseObjectKind,handle,reverse_transition};return LS_None;
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

extern "C" __declspec(dllexport) int WINAPI LoAcquirePenaltyModule(void* handle,void** output) {
    if(!output)return LS_NullOutputParameter;*output=nullptr;
    try {
        std::lock_guard<std::mutex> lock(gate);auto found=contexts.find(handle);
        if(found==contexts.end())return LS_InvalidContext;
        if(found->second->in_use)return LS_ContextInUse;
        auto state=std::make_shared<PenaltyState>();state->context=found->second;
        auto module=std::make_shared<PenaltyModule>();module->state=state;
        void* key=module.get();penalties.emplace(key,module);*output=key;return LS_None;
    }catch(const std::bad_alloc&){return LS_OutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI LoGetPenaltyModuleInternalHandle(void* handle,void** output) {
    if(!output)return LS_NullOutputParameter;*output=nullptr;
    std::lock_guard<std::mutex> lock(gate);auto found=penalties.find(handle);
    if(found==penalties.end())return LS_InvalidParameter;
    *output=found->second->state.get();return LS_None;
}
extern "C" __declspec(dllexport) int WINAPI LoDisposePenaltyModule(void* handle) {
    std::lock_guard<std::mutex> lock(gate);auto found=penalties.find(handle);
    if(found==penalties.end())return LS_InvalidParameter;
    penalties.erase(found);return LS_None;
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
        Context* previous;
        explicit Active(std::shared_ptr<Context> value):context(std::move(value)),previous(reverse_active_context){reverse_active_context=context.get();}
        ~Active() { reverse_active_context=previous;std::lock_guard<std::mutex> lock(gate); context->in_use=false; }
    } active{context};
    const bool minmax=(flags&8)!=0;
    try {
        auto result=std::make_shared<Line>(); result->context=context; result->first=cp; result->paragraph_first=cp;
        if(previous_break) {
            std::lock_guard<std::mutex> lock(gate);
            auto found=records.find(previous_break);
            if(found==records.end() || found->second->next!=cp || found->second->context!=context) return LS_InvalidBreakRecord;
            result->paragraph_first=found->second->paragraph_first;result->initial_level=found->second->level;
        }
        LsPap paragraph{}; LsLineProps properties{};
        int status=reinterpret_cast<FetchPap>(context->info.pfnFetchPap)(context->info.pols,cp,&paragraph);
        if(status) return status;
        if(!previous_break) result->paragraph_first=paragraph.cpFirst;
        status=reinterpret_cast<FetchProps>(context->callbacks.pfnFetchLineProps)(context->info.pols,cp,cp==result->paragraph_first,&properties);
        if(status) return status;
        if((paragraph.fJustify && paragraph.lskj!=0 && paragraph.lskj!=4)) return LS_NotImplemented;
        result->justified=paragraph.fJustify!=0;result->justification=paragraph.lskj;
        int flow=paragraph.lstflow;
        if(flow!=0 && flow!=4) return LS_NotImplemented;
        result->base_flow=flow;if(!previous_break)result->initial_level=flow==4?1:0;int level=result->initial_level;
        int main_start=properties.durLeft;
        if((paragraph.grpf&0x1000) && cp==result->paragraph_first) {
            status=format_marker(*result,properties.durLeft,main_start);if(status)return status;
        } else if((paragraph.grpf&0x1000) && !context->tabs.empty() && context->tabs.front().ur>main_start) {
            // A cached paragraph retains its marker store on later lines. Its
            // implicit tab still names the main-text origin, without repainting
            // the paragraph's marker or using its offset as the body indent.
            main_start=context->tabs.front().ur;
        }
        properties.durLeft=main_start;
        result->widths.upStartMainText=main_start;
        int position=cp,total=main_start,trailing=main_start;
        bool ended=false;
        for(int attempts=0;attempts<9600 && !ended;++attempts) {
            if(limit>0&&position-cp>=limit){ended=true;break;}
            Run run; run.cp=position;run.level=level;run.flow=(level&1)?4:0;
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
            if(run.properties.idObj!=0xFFFF && run.properties.idObj!=1 && run.properties.idObj!=0) return LS_NotImplemented;
            bool reversal=run.properties.idObj==0;
            bool close=run.properties.idObj==0xFFFF&&text[0]==0xFFFB&&(run.id&0x0FFFFFFFu)==0;
            if(reversal||close){
                if(reversal&&text[0]!=0xFFFC)return LS_InvalidFormatterResult;
                if(limit>0)count=std::min(count,limit-(position-cp));
                // Original SpanVector coalesces adjacent identical control IDs;
                // their one-character backing buffer represents count scopes.
                status=transition_reverse(*context,run,flow==4?1:0,level,count,reversal);if(status)return status;
                run.hidden=true;run.text.assign(count,reversal?0xFFFC:0xFFFB);run.widths.assign(count,0);result->runs.push_back(std::move(run));position+=count;continue;
            }
            run.embedded=run.properties.idObj==1;
            if(run.embedded && count!=1) return LS_InvalidDcpFetched;
            run.hidden=hidden_flag!=0 || (run.properties.flags&0x10000)!=0;
            run.text.assign(text,text+count); run.widths.resize(count);
            if(count==1 && (text[0]==0x2028 || text[0]==0x2029)) {
                if(position==INT_MAX) return LS_InvalidFormatterResult;
                result->info.endr=text[0]==0x2029?2:4;
                run.hidden=true; result->terminator=std::move(run); result->has_terminator=true;
                if(minmax && text[0]==0x2028) {
                    result->terminator.hard_break=true;
                    result->terminator.widths.assign(1,0);
                    result->runs.push_back(std::move(result->terminator));result->has_terminator=false;
                    total=properties.durLeft;++position;continue;
                }
                ended=true; ++position; break;
            }
            if(limit>0 && position-cp+count>limit) count=limit-(position-cp);
            if(count<=0) { ended=true; break; }
            auto tab=std::find(text,text+count,L'\t');
            if(tab==text) { count=1; run.tab=true; }
            else if(tab!=text+count) count=static_cast<int>(tab-text);
            run.text.resize(count); run.widths.resize(count);
            if(run.embedded) {
                status=layout_tabs(*result,properties.durLeft,total); if(status) return status;
                status=format_object(*context,run,total,LS_IdealInfiniteWidth);
                if(status) return status;
                if(run.position_used && std::any_of(result->runs.begin(),result->runs.end(),[](const Run& value) { return value.tab; }) &&
                    std::any_of(context->tabs.begin(),context->tabs.end(),[](const LsTbd& value) { return value.lskt!=0; })) return LS_NotImplemented;
            } else status=measure_run(*context,run);
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
        bool needs_tab_layout=false;
        status=layout_tabs(*result,properties.durLeft,total);
        if(status) {
            // With no incremental fallback, a long full field can miss all
            // explicit stops even though a shorter wrapped field fits one.
            if(status!=LS_InvalidFormatterResult || !(paragraph.grpf&0x40) || limit || context->tabs.empty()) return status;
            needs_tab_layout=true;
        }
        // Fetch through the separator before finding Unicode breaks: a client
        // run boundary can be in the middle of a word or grapheme.
        if(!minmax && (paragraph.grpf&0x40) && (needs_tab_layout || total>column-properties.durRightBreak) && limit==0) {
            int max_cut=INT_MAX;
            for(;;) {
                bool unavoidable=false;
                int allowance=column-properties.durRightBreak-properties.durLeft;
                int cut=choose_wrap(*result,allowance,(paragraph.grpf&0x80000)==0||(flags&4),max_cut,unavoidable);
                Run hyphen;int hyphen_cut=0;
                if((paragraph.grpf&0x100000)&&!properties.fProhibitHyphenation){bool overflow=false;int natural=choose_wrap(*result,allowance,false,max_cut,overflow);
                    status=hyphen_opportunity(*result,allowance,max_cut,natural,overflow,hyphen_cut,hyphen);if(status)return status;if(hyphen_cut){cut=hyphen_cut;unavoidable=false;}}
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
                if(hyphen_cut){candidate.runs.push_back(std::move(hyphen));candidate.info.endr=1;candidate.has_terminator=false;}

                status=layout_tabs(candidate,properties.durLeft,total);
                if(status) {
                    if(status==LS_InvalidFormatterResult && cut>1 && !context->tabs.empty()) { max_cut=cut-1; continue; }
                    return status;
                }
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
        if(limit>0 && !(flags&0x40000000u) && result->info.endr==0 && (paragraph.grpf&0x100000) && !properties.fProhibitHyphenation && context->info.pfnGetNextHyphenOpp) {
            auto owner=std::find_if(result->runs.rbegin(),result->runs.rend(),[](const Run& run){return !run.hidden&&!run.embedded&&!run.tab&&!run.text.empty();});
            if(owner!=result->runs.rend()) {
                int last=owner->cp+int(owner->text.size())-1,found=0,at=0;LsHyph detail{};
                // Reconstructed lines have an explicit source length. Ask the
                // current source/culture whether this exact final text CP is a
                // dictionary break; no stale cache hint crosses document owners.
                status=reinterpret_cast<HyphenOpportunity>(context->info.pfnGetNextHyphenOpp)(context->info.pols,last,1,&found,&at,&detail);if(status)return status;
                if(found && (found!=1||at!=last))return LS_WrongHyphenationPosition;
                if(found && detail.kysr!=0)return LS_NotImplemented;
                if(found) {
                    Run marker;status=measure_hyphen(*context,*owner,last,marker);if(status)return status;
                    if(int64_t(total)+marker.width>INT_MAX)return LS_InvalidFormatterResult;
                    total+=marker.width;trailing=total;result->info.endr=1;
                    result->info.dvpAscent=std::max(result->info.dvpAscent,marker.metrics.dvAscent);
                    result->info.dvpDescent=std::max(result->info.dvpDescent,marker.metrics.dvDescent);
                    result->info.dvpMultiLineHeight=std::max(result->info.dvpMultiLineHeight,marker.metrics.dvMultiLineHeight);
                    result->runs.push_back(std::move(marker));
                }
            }
        }
        status=reset_line_whitespace(*result);if(status)return status;
        status=layout_tabs(*result,properties.durLeft,total);if(status)return status;
        trailing=total;
        for(auto item=result->runs.rbegin();item!=result->runs.rend();++item){bool all_spaces=false;trailing-=trailing_width(*item,all_spaces);if(!all_spaces)break;}
        int minimum_total=total,minimum_trailing=trailing;
        if(!minmax && paragraph.fJustify && (result->info.endr==0||result->info.endr==1) && !(flags&0x40000000u)) {
            int64_t target=static_cast<int64_t>(column)-properties.durRightJustify;
            if(target<INT_MIN || target>INT_MAX) return LS_InvalidFormatterResult;
            status=justify_line(*result,static_cast<int>(target),total,trailing); if(status) return status;
        }
        if(minmax) {
            status=paragraph_widths(*result,total,trailing,minimum_total,minimum_trailing);if(status)return status;
        }
        result->info.dvrAscent=result->info.dvpAscent;
        result->info.dvrDescent=result->info.dvpDescent;
        result->info.dvrMultiLineHeight=result->info.dvpMultiLineHeight;
        if(result->runs.empty()) result->info.dvrMultiLineHeight=INT_MAX;
        result->info.cpLimToContinue=result->info.cpLimToStay=position;
        result->info.cpFirstVis=cp; result->info.fFirstLineInPara=cp==result->paragraph_first;
        result->widths.upLimLine=total; result->widths.upStartTrailing=trailing;
        result->widths.upMinLimLine=minimum_total; result->widths.upMinStartTrailing=minimum_trailing;
        void* line_handle=result.get();
        { std::lock_guard<std::mutex> lock(gate); lines.emplace(line_handle,result); }
        *info=result->info;*widths=result->widths;*depth=1;for(const auto& run:result->runs)*depth=std::max(*depth,run.level-(result->base_flow==4?1:0)+1);*output=line_handle;
        return LS_None;
    } catch(const std::bad_alloc&) { return LS_OutOfMemory; }
}

extern "C" __declspec(dllexport) int WINAPI LoCreateParaBreakingSession(void* handle,int cp,int width,
    void* previous,void** output,BOOL* justified) {
    if(!output || !justified)return LS_NullOutputParameter;
    *output=nullptr;*justified=FALSE;
    if(cp<0 || width<=0 || width>LS_IdealInfiniteWidth)return LS_InvalidParameter;
    try {
        auto session=std::make_shared<ParaSession>();
        LsLInfo info{};LsLineWidths widths{};int depth=0;void* whole=nullptr;
        // Prefetch the original managed FullTextState's runs as well as validating
        // the source. Its CP-to-LSCP mapping needs these runs before CreateBreaks.
        int status=LoCreateLine(handle,cp,0,LS_IdealInfiniteWidth,0,previous,&info,&whole,&depth,&widths);
        if(status)return status;
        {
            std::lock_guard<std::mutex> lock(gate);
            auto line=lines.at(whole);lines.erase(whole);
            session->context=line->context;session->first=cp;session->end=info.cpLimToContinue;session->width=width;
            *justified=line->justified;
        }
        // A soft line separator ends a line, not the paragraph cache. Prefetch
        // the remaining hard lines so later LSCP mappings include their runs.
        while(info.endr==4) {
            int next=info.cpLimToContinue;
            status=LoCreateLine(handle,next,0,LS_IdealInfiniteWidth,0,nullptr,&info,&whole,&depth,&widths);
            if(status){*justified=FALSE;return status;}
            {std::lock_guard<std::mutex> lock(gate);lines.erase(whole);}
            if(info.cpLimToContinue<=next){*justified=FALSE;return LS_InvalidFormatterResult;}
            session->end=info.cpLimToContinue;
        }
        {std::lock_guard<std::mutex> lock(gate);sessions.emplace(session.get(),session);*output=session.get();}
        return LS_None;
    } catch(const std::bad_alloc&) { *justified=FALSE;return LS_OutOfMemory; }
}

extern "C" __declspec(dllexport) int WINAPI LoDisposeParaBreakingSession(void* handle,BOOL) {
    std::lock_guard<std::mutex> lock(gate);auto found=sessions.find(handle);
    if(found==sessions.end())return LS_InvalidParameter;
    if(found->second->in_use)return LS_ContextInUse;
    // Returned lines and cloned break records retain their own context/lifetime.
    for(const auto& penalty:found->second->detached)if(penalty.use_count()==1)break_penalties.erase(penalty.get());
    sessions.erase(found);return LS_None;
}

extern "C" __declspec(dllexport) int WINAPI LoCreateBreaks(void* handle,int cp,void* previous,
    void* session_handle,void* restriction,LsBreaks* output,int* best) {
    if(!output || !best)return LS_NullOutputParameter;
    *output={};*best=0;
    std::shared_ptr<ParaSession> session;
    {
        std::lock_guard<std::mutex> lock(gate);auto found=sessions.find(session_handle);
        if(found==sessions.end())return LS_InvalidParameter;
        auto context=contexts.find(handle);if(context==contexts.end())return LS_InvalidContext;
        session=found->second;
        if(context->second!=session->context)return LS_MismatchLineContext;
        if(session->in_use)return LS_ContextInUse;
        if(cp<session->first || cp>=session->end)return LS_WrongCpFirstForGetBreaks;
        if(restriction)return LS_NotImplemented; // No foreign/unknown penalty pointer is dereferenced.
        session->in_use=true;
    }
    struct Active {
        std::shared_ptr<ParaSession> session;
        std::vector<void*> provisional;
        bool committed=false;
        ~Active(){std::lock_guard<std::mutex> lock(gate);if(!committed)for(void* key:provisional){auto at=lines.find(key);if(at!=lines.end()){break_penalties.erase(at->second->penalty.get());lines.erase(at);}}session->in_use=false;}
    } active{session};
    try {
        LsLInfo full_info{};LsLineWidths full_widths{};int depth=0;void* whole=nullptr;
        int status=LoCreateLine(handle,cp,0,LS_IdealInfiniteWidth,0,previous,&full_info,&whole,&depth,&full_widths);
        if(status)return status;
        // Erase the provisional registration before any allocation can fail.
        std::shared_ptr<Line> full;
        {std::lock_guard<std::mutex> lock(gate);full=lines.at(whole);lines.erase(whole);}
        full->paragraph_first=session->first;
        LsLineProps properties{};LsPap paragraph{};
        status=reinterpret_cast<FetchProps>(session->context->callbacks.pfnFetchLineProps)(session->context->info.pols,cp,cp==session->first,&properties);
        if(status)return status;
        properties.durLeft=full->widths.upStartMainText;
        status=reinterpret_cast<FetchPap>(session->context->info.pfnFetchPap)(session->context->info.pols,cp,&paragraph);
        if(status)return status;
        int64_t available=int64_t(session->width)-properties.durRightBreak-properties.durLeft;
        if(available<0 || available>INT_MAX)return LS_InvalidFormatterResult;
        BreakGeometry geometry;status=break_geometry(*full,geometry);if(status)return status;
        UErrorCode error=U_ZERO_ERROR;
        std::unique_ptr<UBreakIterator,decltype(&ubrk_close)> iterator(ubrk_open(UBRK_LINE,"root",geometry.text.data(),int(geometry.text.size()),&error),ubrk_close);
        if(U_FAILURE(error)||!iterator)return LS_InvalidFormatterResult;
        struct Candidate {int cut;bool forced;std::shared_ptr<Run> marker;bool overflow=false;};
        std::vector<Candidate> candidates;
        for(int point=1;point<=int(geometry.text.size());++point) {
            if(!geometry.boundary[point] || !geometry.allowed(point,ubrk_isBoundary(iterator.get(),point)))continue;
            int cut=geometry.raw[point];
            if(point==int(geometry.text.size()) && full->has_terminator)cut=full_info.cpLimToContinue-cp;
            candidates.push_back({cut,false,{}});
        }
        if(geometry.text.empty() && full->has_terminator)candidates.push_back({full_info.cpLimToContinue-cp,false,{}});
        if((paragraph.grpf&0x100000)&&!properties.fProhibitHyphenation) {
            for(int maximum=INT_MAX;maximum>0;) {
                int cut=0;Run marker;
                status=hyphen_opportunity(*full,int(available),maximum,0,true,cut,marker);if(status)return status;
                if(!cut)break;candidates.push_back({cut,false,std::make_shared<Run>(std::move(marker))});maximum=cut-1;
            }
        }
        std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.cut<b.cut || (a.cut==b.cut && bool(a.marker)<bool(b.marker));});
        candidates.erase(std::unique(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.cut==b.cut&&bool(a.marker)==bool(b.marker);}),candidates.end());
        std::vector<LsLInfo> information;std::vector<void*> line_handles,penalty_handles;
        double best_cost=std::numeric_limits<double>::infinity();int best_index=0;
        auto add=[&](const Candidate& candidate)->int {
            LsLInfo info{};LsLineWidths widths{};void* key=nullptr;int count=0;
            // Prefix reconstruction reshapes the actual retained clusters. A
            // nominal full-paragraph width alone cannot establish feasibility.
            // Candidate scores use nominal widths. Public prefix reconstruction
            // applies the selected paragraph's mixed/inter-word justification.
            int result=LoCreateLine(handle,cp,candidate.cut,session->width,0x40000000u,previous,&info,&key,&count,&widths);
            if(result)return result;
            std::shared_ptr<Line> line;
            {std::lock_guard<std::mutex> lock(gate);line=lines.at(key);lines.erase(key);}
            line->paragraph_first=session->first;line->info.fFirstLineInPara=cp==session->first;
            if(candidate.marker) {
                if(int64_t(line->widths.upLimLine)+candidate.marker->width>INT_MAX)return LS_InvalidFormatterResult;
                line->runs.push_back(*candidate.marker);line->has_terminator=false;line->info.endr=1;
                line->widths.upLimLine+=candidate.marker->width;line->widths.upStartTrailing=line->widths.upLimLine;
            }
            int64_t visible=int64_t(line->widths.upStartTrailing)-properties.durLeft;
            if(!candidate.overflow && visible>available)return LS_None;
            if(line->info.cpLimToContinue<=cp)return LS_InvalidFormatterResult;
            auto penalty=std::make_shared<BreakPenalty>();penalty->first=cp;penalty->next=line->info.cpLimToContinue;
            penalty->width=int(visible);penalty->target=int(available);penalty->forced=candidate.forced;
            penalty->hyphenated=bool(candidate.marker);penalty->terminal=line->info.endr==2||line->info.endr==4;
            double slack=double(available-visible)/std::max<int64_t>(1,available);
            penalty->cost=penalty->terminal?0:slack*slack;
            if(penalty->hyphenated)penalty->cost+=0.1;
            if(penalty->forced)penalty->cost+=1000;
            line->penalty=penalty;line->penalty_session=session;line->info.fForcedBreak=candidate.forced;
            // Reserve rollback ownership before publishing a handle.
            active.provisional.push_back(key);
            {std::lock_guard<std::mutex> lock(gate);lines.emplace(key,line);break_penalties.emplace(penalty.get(),penalty);}
            int index=int(information.size());information.push_back(line->info);line_handles.push_back(key);penalty_handles.push_back(penalty.get());
            if(penalty->cost<best_cost || (penalty->cost==best_cost&&penalty->next>information[best_index].cpLimToContinue)){best_cost=penalty->cost;best_index=index;}
            return LS_None;
        };
        for(const auto& candidate:candidates){status=add(candidate);if(status)return status;}
        if(information.empty()) {
            bool unavoidable=false;int cut=choose_wrap(*full,int(available),(paragraph.grpf&0x80000)==0,INT_MAX,unavoidable);
            if(cut<=0)return LS_InvalidFormatterResult;
            bool emergency=(paragraph.grpf&0x80000)==0;
            if(!geometry.raw.empty() && cut==geometry.raw.back() && full->has_terminator)cut=full_info.cpLimToContinue-cp;
            status=add({cut,emergency,{},true});if(status)return status;
        }
        if(information.empty())return LS_InvalidFormatterResult;
        session->information.swap(information);session->line_handles.swap(line_handles);session->penalty_handles.swap(penalty_handles);
        output->cBreaks=int(session->information.size());output->plslinfoArray=session->information.data();
        output->pplolineArray=session->line_handles.data();output->plinepenaltyArray=session->penalty_handles.data();
        *best=best_index;active.committed=true;return LS_None;
    } catch(const std::bad_alloc&) {return LS_OutOfMemory;}
}

extern "C" __declspec(dllexport) int WINAPI LoRelievePenaltyResource(void* handle) {
    std::lock_guard<std::mutex> lock(gate);auto found=lines.find(handle);
    if(found==lines.end())return LS_InvalidLine;
    auto line=found->second;if(!line->penalty)return LS_InvalidParameter;
    if(line->detached_penalty)return LS_None;
    try {
        // The paragraph owns detached penalties until its session is disposed.
        if(auto owner=line->penalty_session.lock()){owner->detached.push_back(line->penalty);line->detached_penalty=true;return LS_None;}
        return LS_InvalidParameter;
    }catch(const std::bad_alloc&){return LS_OutOfMemory;}
}

extern "C" __declspec(dllexport) int WINAPI LoDisposeLine(void* handle,BOOL) {
    std::lock_guard<std::mutex> lock(gate);
    auto found=lines.find(handle); if(found==lines.end()) return LS_InvalidLine;
    if(found->second->penalty && (!found->second->detached_penalty || found->second->penalty_session.expired()))break_penalties.erase(found->second->penalty.get());
    lines.erase(found); return LS_None;
}

// Shared source implementation interface for PTS scoring. Opaque managed
// penalty pointers are validated against our registry before accessing data.
extern "C" int WINAPI WpfReadBreakPenalty(void* handle,double* cost,int* width,int* hyphenated) {
    if(!cost||!width||!hyphenated)return LS_NullOutputParameter;
    *cost=0;*width=0;*hyphenated=0;
    std::lock_guard<std::mutex> lock(gate);auto found=break_penalties.find(handle);
    if(found==break_penalties.end())return LS_InvalidParameter;
    auto penalty=found->second.lock();if(!penalty){break_penalties.erase(found);return LS_InvalidParameter;}
    *cost=penalty->cost;*width=penalty->width;*hyphenated=penalty->hyphenated;return LS_None;
}

extern "C" __declspec(dllexport) int WINAPI LoAcquireBreakRecord(void* handle,void** output) {
    if(!output) return LS_NullOutputParameter; *output=nullptr;
    auto line=get_line(handle); if(!line) return LS_InvalidLine;
    try {
        auto record=std::make_shared<BreakRecord>(BreakRecord{line->info.cpLimToContinue,line->info.endr==2?line->info.cpLimToContinue:line->paragraph_first,line->context,0});
        record->level=line->initial_level;for(const auto& run:line->runs)record->level+=run.level_delta;
        if(line->info.endr==2)record->level=line->base_flow==4?1:0;
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
            return query_cell(*line,cell,capacity,sub,depth,text,cp);
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
            int end=cell.x+(cell.flow==line->base_flow?cell.width:0);if(static_cast<int64_t>(point->x)<end) break;
        }
        if(!chosen) chosen=&cells.front();
        return query_cell(*line,*chosen,capacity,sub,depth,text);
    } catch(const std::bad_alloc&) { return LS_OutOfMemory; }
}

namespace {
int display_runs(Line& line,Context& context,LSPOINT* origin,uint32_t mode,LSRECT* clip) {
    auto geometry=visual_runs(line);
    for(size_t index=0;index<line.runs.size();++index) {
        auto& run=line.runs[index];int x=origin->x+(line.base_flow==4?-geometry.edge[index]:geometry.edge[index]);
        LSPOINT point{x,origin->y};
        LsHeights height{run.metrics.dvAscent,run.metrics.dvDescent,run.metrics.dvMultiLineHeight};
        int status=0;
        if(!run.hidden) {
            if(run.embedded) {
                status=reinterpret_cast<DrawObject>(run.object_handler.pfnDraw)(context.info.pols,
                    static_cast<uint32_t>(run.id),&point,run.flow,run.width);
            } else if(run.tab) {
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
                    run.advances.data(),run.nominal_advances.empty()?run.advances.data():run.nominal_advances.data(),run.offsets.data(),run.glyph_properties.data(),run.expansion.data(),
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
    }
    return LS_None;
}
}
extern "C" __declspec(dllexport) int WINAPI LoDisplayLine(void* handle,LSPOINT* origin,uint32_t mode,LSRECT* clip) {
    if(!origin||!clip)return LS_InvalidParameter;
    auto line=get_line(handle);if(!line)return LS_InvalidLine;
    try {
        if(!line->markers.empty()) {
            Line marker;marker.context=line->context;marker.base_flow=line->base_flow;marker.initial_level=line->base_flow==4?1:0;
            marker.runs=line->markers;marker.widths.upStartMainText=line->widths.upStartMarker;marker.widths.upLimLine=line->widths.upLimMarker;
            int status=display_runs(marker,*line->context,origin,mode,clip);if(status)return status;
        }
        return display_runs(*line,*line->context,origin,mode,clip);
    }catch(const std::bad_alloc&){return LS_OutOfMemory;}
}

extern "C" __declspec(dllexport) int WINAPI LoEnumLine(void* handle,BOOL reverse,BOOL geometry,LSPOINT* origin) {
    if(!origin) return LS_InvalidParameter;
    auto line=get_line(handle); if(!line) return LS_InvalidLine;
    const auto& context=*line->context;
    if(!context.info.pfnEnumText) return LS_NotImplemented;
    try {
    auto geometry_runs=visual_runs(*line);std::vector<int> starts;
    for(int edge:geometry_runs.edge)starts.push_back(origin->x+edge);
    for(size_t index=0;index<line->runs.size();++index) {
        size_t position=reverse?line->runs.size()-index-1:index;
        auto& run=line->runs[position]; if(run.hidden || run.embedded) continue;
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
            run.cp,run.synthetic?0:static_cast<int>(run.text.size()),run.text.data(),static_cast<int>(run.text.size()),run.flow,
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
