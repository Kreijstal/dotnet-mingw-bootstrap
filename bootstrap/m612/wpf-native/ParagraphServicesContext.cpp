// SPDX-License-Identifier: MIT
// Source paragraph contexts and callback-driven finite text-page layout.
#include <windows.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <new>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <atomic>
#include "ParagraphServicesNative.h"
namespace {
struct InstalledObjects { FSIMETHODS methods[2]; };
struct Context {
    FSCONTEXTINFO info{};
    std::shared_ptr<InstalledObjects> installed;
    void* object_contexts[2]{};
    std::atomic<int> resources{0};
};
using CreateObject=int(WINAPI*)(void*,void*,FSCBKOBJ*,uint32_t,int,void**);
using DestroyObject=int(WINAPI*)(void*);
std::mutex gate;
std::unordered_map<void*,std::shared_ptr<InstalledObjects>> objects;
std::unordered_map<void*,std::shared_ptr<Context>> contexts;
int destroy_objects(Context& context) {
    int result=0;
    for(int i=1;i>=0;--i) if(context.object_contexts[i]) {
        void* handle=context.object_contexts[i];context.object_contexts[i]=nullptr;
        int status=reinterpret_cast<DestroyObject>(context.installed->methods[i].pfnDestroyContext)(handle);
        if(!result)result=status;
    }
    return result;
}
}
extern "C" __declspec(dllexport) int WINAPI CreateInstalledObjectsInfo(const FSIMETHODS* subtrack,const FSIMETHODS* subpage,void** output,int* count) {
    if(!output||!count)return PTS_tserrNullOutputParameter;*output=nullptr;*count=0;
    if(!subtrack||!subpage||!subtrack->pfnCreateContext||!subtrack->pfnDestroyContext||!subpage->pfnCreateContext||!subpage->pfnDestroyContext)return PTS_tserrInvalidParameter;
    try {
        auto value=std::make_shared<InstalledObjects>();value->methods[0]=*subtrack;value->methods[1]=*subpage;
        void* handle=value.get();{std::lock_guard<std::mutex> lock(gate);objects.emplace(handle,value);}
        // Original PtsHost assigns IDs 0/1 to container/subpage and 2/3 to
        // floater/table callbacks. The returned array is opaque to C# clients.
        *output=handle;*count=4;return 0;
    } catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI DestroyInstalledObjectsInfo(void* handle) {
    std::lock_guard<std::mutex> lock(gate);auto found=objects.find(handle);
    if(found==objects.end())return PTS_tserrInvalidParameter;
    objects.erase(found);return 0;
}
extern "C" __declspec(dllexport) int WINAPI CreateDocContext(const FSCONTEXTINFO* info,void** output) {
    if(!output)return PTS_tserrNullOutputParameter;*output=nullptr;
    if(!info||info->version||info->drMinColumnBalancingStep<0||info->cInstalledObjects!=4)return PTS_tserrInvalidParameter;
    try {
        auto context=std::make_shared<Context>();context->info=*info;
        {
            std::lock_guard<std::mutex> lock(gate);auto found=objects.find(info->pInstalledObjects);
            if(found==objects.end())return PTS_tserrInvalidParameter;
            context->installed=found->second;
        }
        for(int i=0;i<2;++i) {
            int status=reinterpret_cast<CreateObject>(context->installed->methods[i].pfnCreateContext)(info->pfsclient,
                context.get(),&context->info.fscbk.cbkobj,info->fsffi,i,&context->object_contexts[i]);
            if(status || !context->object_contexts[i]) { destroy_objects(*context);return status?status:PTS_tserrInvalidFormatterResult; }
        }
        try {
            void* handle=context.get();{std::lock_guard<std::mutex> lock(gate);contexts.emplace(handle,context);}
            *output=handle;return 0;
        } catch(const std::bad_alloc&) { destroy_objects(*context);return PTS_tserrOutOfMemory; }
    } catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI DestroyDocContext(void* handle) {
    std::shared_ptr<Context> context;
    {std::lock_guard<std::mutex> lock(gate);auto found=contexts.find(handle);
        if(found==contexts.end())return PTS_tserrInvalidLsContext;
        if(found->second->resources.load()!=0)return PTS_tserrInvalidPtsContext;
        context=found->second;contexts.erase(found);}
    return destroy_objects(*context);
}
// Diagnostic API copies retained ABI records; it does not claim page layout.
extern "C" __declspec(dllexport) int WINAPI WpfGetParagraphAbiSizes(int* context,int* methods,int* callbacks) {
    if(!context||!methods||!callbacks)return PTS_tserrNullOutputParameter;
    *context=sizeof(FSCONTEXTINFO);*methods=sizeof(FSIMETHODS);*callbacks=sizeof(FSCBK);return 0;
}
extern "C" __declspec(dllexport) int WINAPI WpfGetParagraphContextState(void* handle,FSCONTEXTINFO* info,FSIMETHODS* methods,void** object_contexts) {
    if(!info||!methods||!object_contexts)return PTS_tserrNullOutputParameter;
    *info={};methods[0]=methods[1]={};object_contexts[0]=object_contexts[1]=nullptr;
    std::lock_guard<std::mutex> lock(gate);auto found=contexts.find(handle);if(found==contexts.end())return PTS_tserrInvalidLsContext;
    const auto& context=*found->second;*info=context.info;
    for(int i=0;i<2;++i){methods[i]=context.installed->methods[i];object_contexts[i]=context.object_contexts[i];}
    return 0;
}
// Enumerate source-derived field sizes/offsets for the managed ABI gate. Both
// union arms are included so a matching outer size cannot hide wrong fields.
extern "C" __declspec(dllexport) int WINAPI WpfGetParagraphAbiField(int index,const char** type,const char** field,int* size,int* offset) {
    if(!type||!field||!size||!offset)return PTS_tserrNullOutputParameter;
    *type=*field=nullptr;*size=*offset=0;
    constexpr int count=sizeof(WpfParagraphAbiFields)/sizeof(WpfParagraphAbiFields[0]);
    if(index<0||index>=count)return PTS_tserrInvalidParameter;
    const auto& value=WpfParagraphAbiFields[index];
    *type=value.type;*field=value.field;*size=value.size;*offset=value.offset;return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsTransformRectangle(uint32_t input,const FSRECT* page,const FSRECT* rect,uint32_t output,FSRECT* result) {
    if(!result)return PTS_tserrNullOutputParameter;
    // WPF passes the same ref/out variable at numerous call sites.
    if(!page||!rect){*result={};return PTS_tserrInvalidParameter;}
    const FSRECT p=*page,r=*rect;*result={};
    if(input>7||output>7||p.du<0||p.dv<0||r.du<0||r.dv<0)return PTS_tserrInvalidParameter;
    using Wide=int64_t;
    const bool in_vertical=(input&PTS_fUVertical)!=0,out_vertical=(output&PTS_fUVertical)!=0;
    Wide u=Wide(r.u)-p.u,v=Wide(r.v)-p.v,du=r.du,dv=r.dv;
    if(input&PTS_fUDirection)u=(in_vertical?p.dv:p.du)-u-du;
    if(input&PTS_fVDirection)v=(in_vertical?p.du:p.dv)-v-dv;
    if(in_vertical){std::swap(u,v);std::swap(du,dv);}
    if(out_vertical){std::swap(u,v);std::swap(du,dv);}
    if(output&PTS_fUDirection)u=(out_vertical?p.dv:p.du)-u-du;
    if(output&PTS_fVDirection)v=(out_vertical?p.du:p.dv)-v-dv;
    u+=p.u;v+=p.v;
    if(u<INT32_MIN||u>INT32_MAX||v<INT32_MIN||v>INT32_MAX)return PTS_tserrInvalidParameter;
    *result={int32_t(u),int32_t(v),int32_t(du),int32_t(dv)};return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsTransformBbox(uint32_t input,const FSRECT* page,const FSBBOX* bbox,uint32_t output,FSBBOX* result) {
    if(!result)return PTS_tserrNullOutputParameter;
    if(!bbox){*result={};return PTS_tserrInvalidParameter;}
    const FSBBOX original=*bbox;*result={};
    if(!page||input>7||output>7||page->du<0||page->dv<0)return PTS_tserrInvalidParameter;
    if(!original.fDefined)return 0;
    FSRECT transformed{};int status=FsTransformRectangle(input,page,&original.fsrc,output,&transformed);
    if(!status)*result={original.fDefined,transformed};return status;
}

namespace {
void erase_text(void* handle);
struct TextData {
    std::shared_ptr<Context> context;
    explicit TextData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    uint32_t direction=0;
    std::vector<FSLINEDESCRIPTIONSINGLE> lines;
    // Output break records include the last record, which is not necessarily
    // referenced by another line. Retain each record exactly once.
    std::vector<void*> breaks;
    ~TextData() {
        erase_text(this);
        auto& cb=context->info.fscbk.cbktxt;void* client=context->info.pfsclient;
        for(auto& line:lines)if(line.pfslineclient)reinterpret_cast<PtsDestroyLine>(cb.pfnDestroyLine)(client,line.pfslineclient);
        for(void* value:breaks)if(value)reinterpret_cast<PtsDestroyLineBreakRecord>(cb.pfnDestroyLineBreakRecord)(client,value);
        --context->resources;
    }
};
struct ParaData {
    std::shared_ptr<Context> context;
    FSPARADESCRIPTION desc{};
    std::shared_ptr<TextData> text;
    ~ParaData() {
        if(desc.idobj==0 && desc.pfspara)
            reinterpret_cast<PtsObjDestroyPara>(context->installed->methods[0].pfnDestroyPara)(desc.pfspara);
        text.reset();
        if(desc.pfsparaclient)reinterpret_cast<PtsDestroyParaclient>(context->info.fscbk.cbkgen.pfnDestroyParaclient)(context->info.pfsclient,desc.pfsparaclient);
    }
};
struct TrackData {
    std::shared_ptr<Context> context;
    explicit TrackData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~TrackData(){paras.clear();--context->resources;}
    void* segment=nullptr;
    FSRECT rect{};
    FSBBOX bbox{};
    int used=0;
    bool subtrack=false;
    FSKUPDATE update=fskupdNew;
    std::vector<std::shared_ptr<ParaData>> paras;
};
struct PageData {
    std::shared_ptr<Context> context;
    explicit PageData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~PageData(){track.reset();--context->resources;}
    std::shared_ptr<TrackData> track;
    FSPAGEDETAILS details{};
};
std::unordered_map<void*,std::shared_ptr<TrackData>> tracks;
std::unordered_map<void*,std::weak_ptr<TextData>> texts;
std::unordered_map<void*,std::shared_ptr<PageData>> pages;
void erase_text(void* handle){std::lock_guard<std::mutex> lock(gate);texts.erase(handle);}
// A managed object callback may successfully format a native subtrack and then
// throw, resetting its out handle. Track those exports across callback reentry
// so the enclosing failure still releases the now-unreported native resources.
static thread_local std::vector<std::shared_ptr<TrackData>>* callback_tracks=nullptr;
struct CallbackTracks {
    std::vector<std::shared_ptr<TrackData>> created;
    std::vector<std::shared_ptr<TrackData>>* previous;
    void* claimed=nullptr;
    CallbackTracks():previous(callback_tracks){callback_tracks=&created;}
    ~CallbackTracks(){
        callback_tracks=previous;
        for(auto& track:created)if(track.get()!=claimed){
            {std::lock_guard<std::mutex> lock(gate);tracks.erase(track.get());}
            track.reset();
        }
    }
};

std::shared_ptr<Context> find_context(void* handle) {
    std::lock_guard<std::mutex> lock(gate);auto it=contexts.find(handle);
    return it==contexts.end()?nullptr:it->second;
}
template<class T> std::shared_ptr<T> lookup_owned(const std::unordered_map<void*,std::shared_ptr<T>>& map,void* context,void* handle) {
    std::lock_guard<std::mutex> lock(gate);auto it=map.find(handle);
    if(contexts.find(context)==contexts.end()||it==map.end()||it->second->context.get()!=context)return {};
    return it->second;
}
std::shared_ptr<TextData> find_text(void* context,void* handle) {
    std::lock_guard<std::mutex> lock(gate);auto it=texts.find(handle);
    if(contexts.find(context)==contexts.end()||it==texts.end())return {};
    auto value=it->second.lock();
    if(!value||value->context.get()!=context){if(!value)texts.erase(it);return {};}
    return value;
}
struct MarginOwner {
    std::shared_ptr<Context> context;
    void* value=nullptr;
    ~MarginOwner(){reset();}
    void reset(){if(value){void* old=value;value=nullptr;reinterpret_cast<PtsDestroyMcsclient>(context->info.fscbk.cbkobj.pfnDestroyMcsclient)(context->info.pfsclient,old);}}
};
int merge_bbox(FSBBOX& box,const FSBBOX& add) {
    if(!add.fDefined)return 0;
    if(add.fsrc.du<0||add.fsrc.dv<0||int64_t(add.fsrc.u)+add.fsrc.du>INT32_MAX||int64_t(add.fsrc.v)+add.fsrc.dv>INT32_MAX)return PTS_tserrInvalidClientOutput;
    if(!box.fDefined){box=add;return 0;}
    int64_t left=std::min(box.fsrc.u,add.fsrc.u),top=std::min(box.fsrc.v,add.fsrc.v);
    int64_t right=std::max(int64_t(box.fsrc.u)+box.fsrc.du,int64_t(add.fsrc.u)+add.fsrc.du);
    int64_t bottom=std::max(int64_t(box.fsrc.v)+box.fsrc.dv,int64_t(add.fsrc.v)+add.fsrc.dv);
    if(right-left>INT32_MAX||bottom-top>INT32_MAX)return PTS_tserrInvalidClientOutput;
    box.fsrc={int(left),int(top),int(right-left),int(bottom-top)};return 0;
}

int format_text(const std::shared_ptr<ParaData>& para,uint32_t direction,const FSRECT& rect) {
    auto context=para->context;auto& cb=context->info.fscbk.cbktxt;void* client=context->info.pfsclient;
    if(!cb.pfnGetTextProperties||!cb.pfnFormatLine||!cb.pfnDestroyLine||!cb.pfnDestroyLineBreakRecord)return PTS_tserrInvalidParameter;
    FSTXTPROPS props{};int status=reinterpret_cast<PtsGetTextProperties>(cb.pfnGetTextProperties)(client,para->desc.nmp,0,&props);
    if(status)return status;
    if(props.fswdir!=direction||props.fDropCap||props.fVerticalGrid||props.fOptimizeParagraph||props.fKeepTogether)return PTS_tserrNotImplemented;
    auto text=std::make_shared<TextData>(context);text->direction=direction;para->text=text;
    int dcp=props.dcpStartContent,used=0;void* previous=nullptr;
    while(true) {
        // Allocate ownership slots before calling a client that allocates
        // unmanaged handles: bad_alloc afterwards must not leak those handles.
        text->lines.emplace_back();text->breaks.push_back(nullptr);
        auto& line=text->lines.back();auto& br=text->breaks.back();
        line.dcpFirst=dcp;line.urStart=0;line.dur=rect.du;line.vrStart=used;
        line.pfsbreakreclineclient=previous;line.fTreatedAsFirst=(dcp==props.dcpStartContent);
        int consumed=0,dependent=0,reformat=0;FSFLRES result{};
        status=reinterpret_cast<PtsFormatLine>(cb.pfnFormatLine)(client,para->desc.pfsparaclient,para->desc.nmp,0,dcp,previous,direction,
            0,rect.du,rect.u,rect.du,rect.u,0,0,0,line.fTreatedAsFirst,0,used==0,
            &line.pfslineclient,&consumed,&br,&line.fForceBroken,&result,&line.dvrAscent,&line.dvrDescent,&line.urBBox,&line.durBBox,&dependent,&reformat);
        if(status)return status;
        if(!line.pfslineclient||consumed<=0||line.dvrAscent<0||line.dvrDescent<0||line.durBBox<0||int64_t(rect.u)+line.urBBox<INT32_MIN||int64_t(rect.u)+line.urBBox+line.durBBox>INT32_MAX||int64_t(dcp)+consumed>INT32_MAX)return PTS_tserrInvalidClientOutput;
        int64_t height=int64_t(line.dvrAscent)+line.dvrDescent;
        if(height<=0||height>int64_t(rect.dv)-used)return PTS_tserrNotImplemented; // continuation requires a real page break record
        line.dcpLim=dcp+consumed;used+=int(height);dcp=line.dcpLim;previous=br;
        FSBBOX bbox{1,{rect.u+line.urBBox,rect.v+line.vrStart,line.durBBox,int(height)}};status=merge_bbox(para->desc.fsbbox,bbox);if(status)return status;
        if(result>=fsflrEndOfParagraph&&result<=fsflrEndOfParagraphClearBoth)break;
        if(result!=fsflrOutOfSpace&&result!=fsflrSoftBreak)return PTS_tserrNotImplemented;
    }
    para->desc.pfspara=text.get();para->desc.dvrUsed=used;
    {std::lock_guard<std::mutex> lock(gate);texts[text.get()]=text;}
    return 0;
}

int format_track(const std::shared_ptr<Context>& context,void* segment,uint32_t direction,const FSRECT& rect,
    int suppress,void* incoming,std::shared_ptr<TrackData>& result,void** margin_output) {
    auto& cb=context->info.fscbk.cbkgen;void* client=context->info.pfsclient;
    if(!segment||direction!=PTS_fswdirES||rect.du<=0||rect.dv<0||int64_t(rect.u)+rect.du>INT32_MAX||int64_t(rect.v)+rect.dv>INT32_MAX)return PTS_tserrNotImplemented;
    if(!cb.pfnGetFirstPara||!cb.pfnGetNextPara||!cb.pfnGetParaProperties||!cb.pfnCreateParaclient||!cb.pfnDestroyParaclient||!context->info.fscbk.cbkobj.pfnDestroyMcsclient)return PTS_tserrInvalidParameter;
    auto track=std::make_shared<TrackData>(context);track->segment=segment;track->rect=rect;
    int found=0,status=0;void* name=nullptr;
    status=reinterpret_cast<PtsGetFirstPara>(cb.pfnGetFirstPara)(client,segment,&found,&name);if(status)return status;
    static thread_local std::vector<std::pair<void*,void*>> active;
    const auto key=std::make_pair(static_cast<void*>(context.get()),segment);
    if(active.size()>=128 || std::find(active.begin(),active.end(),key)!=active.end())return PTS_tserrInvalidClientOutput;
    active.push_back(key);
    struct Pop {std::vector<std::pair<void*,void*>>& value;~Pop(){value.pop_back();}} pop{active};
    std::unordered_set<void*> visited;MarginOwner margin{context};void* input_margin=incoming;
    while(found) {
        if(!name||!visited.insert(name).second)return PTS_tserrInvalidClientOutput;
        FSPAP props{};status=reinterpret_cast<PtsGetParaProperties>(cb.pfnGetParaProperties)(client,name,&props);if(status)return status;
        if(props.idobj!=-1&&props.idobj!=0)return PTS_tserrNotImplemented;
        if(props.fKeepWithNext||props.fBreakPageBefore||props.fBreakColumnBefore)return PTS_tserrNotImplemented;
        auto para=std::make_shared<ParaData>();para->context=context;para->desc.nmp=name;para->desc.idobj=props.idobj;para->desc.fsupdinf.fskupd=fskupdNew;
        status=reinterpret_cast<PtsCreateParaclient>(cb.pfnCreateParaclient)(client,name,&para->desc.pfsparaclient);if(status)return status;
        if(!para->desc.pfsparaclient)return PTS_tserrInvalidClientOutput;
        FSRECT available=rect;available.v+=track->used;available.dv-=track->used;
        if(props.idobj==-1)status=format_text(para,direction,available);
        else {
            auto& methods=context->installed->methods[0];
            if(!methods.pfnFormatParaFinite||!methods.pfnDestroyPara)return PTS_tserrInvalidParameter;
            CallbackTracks pending;
            FSFMTR fmtr{};void* break_output=nullptr;void* margin_next=nullptr;FSKCLEAR clear{};int can_break=0;
            status=reinterpret_cast<PtsObjFormatParaFinite>(methods.pfnFormatParaFinite)(context->object_contexts[0],para->desc.pfsparaclient,nullptr,0,name,0,nullptr,nullptr,
                0,suppress&&track->paras.empty(),direction,&available,input_margin,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,0,
                &fmtr,&para->desc.pfspara,&break_output,&para->desc.dvrUsed,&para->desc.fsbbox,&margin_next,&clear,&para->desc.dvrTopSpace,&can_break);
            pending.claimed=para->desc.pfspara;
            margin.reset();margin.value=margin_next;input_margin=margin.value;
            if(!status&&(fmtr.kstop!=fmtrGoalReached||break_output||clear!=fskclearNone))status=PTS_tserrNotImplemented;
        }
        if(status)return status;
        if(para->desc.dvrUsed<0||para->desc.dvrUsed>available.dv)return PTS_tserrInvalidClientOutput;
        track->used+=para->desc.dvrUsed;status=merge_bbox(track->bbox,para->desc.fsbbox);if(status)return status;track->paras.push_back(para);
        void* next=nullptr;status=reinterpret_cast<PtsGetNextPara>(cb.pfnGetNextPara)(client,segment,name,&found,&next);if(status)return status;name=next;
    }
    result=track;*margin_output=margin.value;margin.value=nullptr;return 0;
}
void clear_track(const std::shared_ptr<TrackData>& track) {
    track->update=fskupdNoChange;
    for(const auto& para:track->paras) {
        para->desc.fsupdinf.fskupd=fskupdNoChange;
        if(para->desc.idobj==0){auto child=lookup_owned(tracks,track->context.get(),para->desc.pfspara);if(child)clear_track(child);}
    }
}
}
extern "C" __declspec(dllexport) int WINAPI FsFormatSubtrackFinite(void* context_handle,void* break_input,int previous_page,void* segment,int area,
    void* footnote,void* geometry,int empty_ok,int suppress,uint32_t direction,const FSRECT* rect,void* margin_input,
    FSKCLEAR clear,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA suppress_break,FSFMTR* formatted,void** output,void** break_output,
    int* used,FSBBOX* bbox,void** margin_output,FSKCLEAR* clear_output,int* top_space) {
    if(!formatted||!output||!break_output||!used||!bbox||!margin_output||!clear_output||!top_space)return PTS_tserrNullOutputParameter;
    *formatted={};*output=*break_output=*margin_output=nullptr;*used=*top_space=0;*bbox={};*clear_output=fskclearNone;
    auto context=find_context(context_handle);if(!context)return PTS_tserrInvalidPtsContext;
    if(!rect)return PTS_tserrInvalidParameter;
    if(break_input||area||footnote||geometry||clear!=fskclearNone)return PTS_tserrNotImplemented;
    try {
        std::shared_ptr<TrackData> track;MarginOwner margin{context};
        int status=format_track(context,segment,direction,*rect,suppress,margin_input,track,&margin.value);if(status)return status;
        track->subtrack=true;
        if(callback_tracks)callback_tracks->push_back(track);
        {std::lock_guard<std::mutex> lock(gate);tracks.emplace(track.get(),track);}
        *output=track.get();*used=track->used;*bbox=track->bbox;*margin_output=margin.value;margin.value=nullptr;formatted->kstop=fmtrGoalReached;return 0;
    }catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsDestroySubtrack(void* context_handle,void* handle) {
    std::shared_ptr<TrackData> track;
    {std::lock_guard<std::mutex> lock(gate);auto found=tracks.find(handle);
        if(found==tracks.end()||!found->second->subtrack||found->second->context.get()!=context_handle||contexts.find(context_handle)==contexts.end())return PTS_tserrInvalidQuery;
        track=found->second;tracks.erase(found);}
    return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsCreatePageFinite(void* context_handle,void* break_input,void* section,FSFMTR* formatted,void** output,void** break_output) {
    if(!formatted||!output||!break_output)return PTS_tserrNullOutputParameter;
    *formatted={};*output=*break_output=nullptr;
    auto context=find_context(context_handle);if(!context)return PTS_tserrInvalidPtsContext;
    if(break_input)return PTS_tserrNotImplemented;
    auto& cb=context->info.fscbk.cbkgen;void* client=context->info.pfsclient;
    if(!section||!cb.pfnGetPageDimensions||!cb.pfnGetSectionProperties||!cb.pfnGetMainTextSegment)return PTS_tserrInvalidParameter;
    try {
        FSRECT rect{};uint32_t direction=0,section_direction=0;int header=0,width=0,height=0;
        int status=reinterpret_cast<PtsGetPageDimensions>(cb.pfnGetPageDimensions)(client,section,&direction,&header,&width,&height,&rect);if(status)return status;
        int new_page=0,balance=0,columns=0,segments=0,heights=0;
        status=reinterpret_cast<PtsGetSectionProperties>(cb.pfnGetSectionProperties)(client,section,&new_page,&section_direction,&balance,&columns,&segments,&heights);if(status)return status;
        if(direction!=PTS_fswdirES||section_direction!=direction||header||columns!=1||segments||heights||balance)return PTS_tserrNotImplemented;
        if(width<=0||height<=0||rect.u<0||rect.v<0||int64_t(rect.u)+rect.du>width||int64_t(rect.v)+rect.dv>height)return PTS_tserrInvalidGeometry;
        void* segment=nullptr;status=reinterpret_cast<PtsGetMainTextSegment>(cb.pfnGetMainTextSegment)(client,section,&segment);if(status)return status;
        auto page=std::make_shared<PageData>(context);MarginOwner margin{context};
        status=format_track(context,segment,direction,rect,1,nullptr,page->track,&margin.value);if(status)return status;
        auto& details=page->details;details.fskupd=fskupdNew;details.fSimple=1;
        auto& desc=details.u.simple.trackdescr;desc.fsupdinf.fskupd=fskupdNew;desc.nms=segment;desc.fsrc=rect;desc.fsbbox=page->track->bbox;desc.pfstrack=page->track.get();
        {std::lock_guard<std::mutex> lock(gate);
            tracks.emplace(page->track.get(),page->track);
            try {pages.emplace(page.get(),page);}catch(...){tracks.erase(page->track.get());throw;}}
        formatted->kstop=fmtrGoalReached;*output=page.get();return 0;
    }catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsDestroyPage(void* context_handle,void* handle) {
    std::shared_ptr<PageData> page;
    {std::lock_guard<std::mutex> lock(gate);auto found=pages.find(handle);
        if(found==pages.end()||found->second->context.get()!=context_handle||contexts.find(context_handle)==contexts.end())return PTS_tserrInvalidQuery;
        page=found->second;pages.erase(found);tracks.erase(page->track.get());}
    return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQueryPageDetails(void* context,void* handle,FSPAGEDETAILS* output) {
    if(!output)return PTS_tserrNullOutputParameter;*output={};auto page=lookup_owned(pages,context,handle);
    if(!page)return PTS_tserrInvalidQuery;*output=page->details;return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQueryTrackDetails(void* context,void* handle,FSTRACKDETAILS* output) {
    if(!output)return PTS_tserrNullOutputParameter;*output={};auto track=lookup_owned(tracks,context,handle);
    if(!track)return PTS_tserrInvalidQuery;output->cParas=int(track->paras.size());return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQuerySubtrackDetails(void* context,void* handle,FSSUBTRACKDETAILS* output) {
    if(!output)return PTS_tserrNullOutputParameter;*output={};auto track=lookup_owned(tracks,context,handle);
    if(!track||!track->subtrack)return PTS_tserrInvalidQuery;output->fsupdinf.fskupd=track->update;output->nms=track->segment;output->fsrc=track->rect;output->cParas=int(track->paras.size());return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQueryTrackParaList(void* context,void* handle,int capacity,FSPARADESCRIPTION* output,int* count) {
    if(!count)return PTS_tserrNullOutputParameter;*count=0;
    auto track=lookup_owned(tracks,context,handle);if(!track)return PTS_tserrInvalidQuery;
    if(capacity<0||capacity<int(track->paras.size())||(!output&&!track->paras.empty()))return PTS_tserrInvalidParameter;
    for(const auto& para:track->paras)output[(*count)++]=para->desc;return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQuerySubtrackParaList(void* context,void* handle,int capacity,FSPARADESCRIPTION* output,int* count) {
    return FsQueryTrackParaList(context,handle,capacity,output,count);
}
extern "C" __declspec(dllexport) int WINAPI FsQueryTextDetails(void* context,void* handle,FSTEXTDETAILS* output) {
    if(!output)return PTS_tserrNullOutputParameter;*output={};auto text=find_text(context,handle);if(!text)return PTS_tserrInvalidQuery;
    output->fsktd=fsktdFull;auto& value=output->u.full;value.fswdir=text->direction;value.fsklines=fsklinesNormal;value.cLines=int(text->lines.size());
    if(!text->lines.empty()){value.dcpFirst=text->lines.front().dcpFirst;value.dcpLim=text->lines.back().dcpLim;}return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQueryLineListSingle(void* context,void* handle,int capacity,FSLINEDESCRIPTIONSINGLE* output,int* count) {
    if(!count)return PTS_tserrNullOutputParameter;*count=0;auto text=find_text(context,handle);if(!text)return PTS_tserrInvalidQuery;
    if(capacity<0||capacity<int(text->lines.size())||(!output&&!text->lines.empty()))return PTS_tserrInvalidParameter;
    for(const auto& line:text->lines)output[(*count)++]=line;return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsClearUpdateInfoInPage(void* context,void* handle) {
    auto page=lookup_owned(pages,context,handle);if(!page)return PTS_tserrInvalidQuery;
    page->details.fskupd=fskupdNoChange;page->details.u.simple.trackdescr.fsupdinf.fskupd=fskupdNoChange;clear_track(page->track);return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsClearUpdateInfoInSubtrack(void* context,void* handle) {
    auto track=lookup_owned(tracks,context,handle);if(!track)return PTS_tserrInvalidQuery;clear_track(track);return 0;
}
