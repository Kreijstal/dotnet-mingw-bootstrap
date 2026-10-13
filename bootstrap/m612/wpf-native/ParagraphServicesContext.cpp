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
// The handler record is opaque to managed callers. Both its producer and
// consumer belong to this replacement; its callback table comes from Pts.cs.
struct FloaterHandler { uint32_t magic=0; FSFLOATERINIT init{}; };
constexpr uint32_t floater_magic=0x464c5431;
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
extern "C" __declspec(dllexport) int WINAPI GetFloaterHandlerInfo(const FSFLOATERINIT* init,void* output) {
    if(!output)return PTS_tserrNullOutputParameter;
    if(!init)return PTS_tserrInvalidParameter;
    auto handler=static_cast<FloaterHandler*>(output);handler->init=*init;handler->magic=floater_magic;return 0;
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
struct BreakData;
struct LineBreakData {
    std::shared_ptr<Context> context;
    void* handle=nullptr;
    explicit LineBreakData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~LineBreakData(){if(handle)reinterpret_cast<PtsDestroyLineBreakRecord>(context->info.fscbk.cbktxt.pfnDestroyLineBreakRecord)(context->info.pfsclient,handle);--context->resources;}
};
struct Continuation {
    std::shared_ptr<Context> context;
    void* segment=nullptr;
    void* paragraph=nullptr;
    int dcp=0;
    std::shared_ptr<LineBreakData> line_break;
    std::shared_ptr<BreakData> child;
    int child_object=0;
    ~Continuation();
};
struct BreakData {
    std::shared_ptr<Context> context;
    std::shared_ptr<Continuation> position;
    void* section=nullptr;
    bool page=false;
    bool subpage=false;
    explicit BreakData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~BreakData(){position.reset();--context->resources;}
};
Continuation::~Continuation() {
    if(child)reinterpret_cast<PtsObjDestroyBreakRecord>(context->installed->methods[child_object].pfnDestroyBreakRecord)(context->object_contexts[child_object],child.get());
}

struct TextData {
    std::shared_ptr<Context> context;
    explicit TextData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    uint32_t direction=0;
    std::vector<FSLINEDESCRIPTIONSINGLE> lines;
    // Output break records include the last record, which is not necessarily
    // referenced by another line. Retain each record exactly once.
    std::vector<std::shared_ptr<LineBreakData>> breaks;
    std::shared_ptr<LineBreakData> incoming;
    ~TextData() {
        erase_text(this);
        auto& cb=context->info.fscbk.cbktxt;void* client=context->info.pfsclient;
        for(auto& line:lines)if(line.pfslineclient)reinterpret_cast<PtsDestroyLine>(cb.pfnDestroyLine)(client,line.pfslineclient);
        breaks.clear();incoming.reset();
        --context->resources;
    }
};
struct FloaterData;
void erase_floater(void* handle);
struct FloaterData {
    std::shared_ptr<Context> context;
    FSFLOATERCBK callbacks{};
    FSFLOATERDETAILS details{};
    void* break_record=nullptr;
    explicit FloaterData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~FloaterData() {
        erase_floater(this);
        if(break_record)reinterpret_cast<PtsDestroyFloaterContentBreakRecord>(callbacks.pfnDestroyFloaterContentBreakRecord)(context->info.pfsclient,break_record);
        if(details.pfsFloaterContent)reinterpret_cast<PtsDestroyFloaterContent>(callbacks.pfnDestroyFloaterContent)(details.pfsFloaterContent);
        --context->resources;
    }
};
struct ParaData {
    std::shared_ptr<Context> context;
    FSPARADESCRIPTION desc{};
    bool forced_progress=false;
    std::shared_ptr<TextData> text;
    std::shared_ptr<FloaterData> floater;
    ~ParaData();
};
struct TrackData {
    std::shared_ptr<Context> context;
    explicit TrackData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~TrackData(){paras.clear();--context->resources;}
    void* segment=nullptr;
    FSRECT rect{};
    FSBBOX bbox{};
    int used=0;
    std::shared_ptr<Continuation> continuation;
    FSFMTRKSTOP stop=fmtrGoalReached;
    bool forced_progress=false;
    bool subtrack=false;
    bool bottomless=false;
    FSKUPDATE update=fskupdNew;
    std::vector<std::shared_ptr<ParaData>> paras;
};
void erase_section(void* handle);
struct SectionData {
    std::shared_ptr<Context> context;
    explicit SectionData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~SectionData(){erase_section(this);columns.clear();--context->resources;}
    FSSECTIONDESCRIPTION description{};
    std::vector<std::shared_ptr<TrackData>> columns;
    FSKUPDATE update=fskupdNew;
};
struct PageData {
    std::shared_ptr<Context> context;
    explicit PageData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~PageData(){track.reset();section.reset();--context->resources;}
    std::shared_ptr<TrackData> track;
    std::shared_ptr<SectionData> section;
    FSPAGEDETAILS details{};
    bool bottomless=false;
    bool subpage=false;
    FSSUBPAGEDETAILS subpage_details{};
    std::shared_ptr<Continuation> continuation;
    FSFMTRKSTOP stop=fmtrGoalReached;
    bool forced_progress=false;
};
std::unordered_map<void*,std::shared_ptr<TrackData>> tracks;
std::unordered_map<void*,std::weak_ptr<TextData>> texts;
std::unordered_map<void*,std::weak_ptr<FloaterData>> floaters;
std::unordered_map<void*,std::shared_ptr<PageData>> pages;
std::unordered_map<void*,std::shared_ptr<BreakData>> records;
std::unordered_map<void*,std::weak_ptr<SectionData>> sections;
void erase_section(void* handle){std::lock_guard<std::mutex> lock(gate);sections.erase(handle);}
void erase_text(void* handle){std::lock_guard<std::mutex> lock(gate);texts.erase(handle);}
void erase_floater(void* handle){std::lock_guard<std::mutex> lock(gate);floaters.erase(handle);}
ParaData::~ParaData() {
    // Arranged original subpage clients own their subpage handle themselves.
    // Let them dispose it first; an unarranged client leaves native ownership
    // here. Container clients retain the original object-before-client order.
    if(desc.idobj==1&&desc.pfsparaclient){reinterpret_cast<PtsDestroyParaclient>(context->info.fscbk.cbkgen.pfnDestroyParaclient)(context->info.pfsclient,desc.pfsparaclient);desc.pfsparaclient=nullptr;}
    bool destroy_subpage=false;
    if(desc.idobj==1&&desc.pfspara){std::lock_guard<std::mutex> lock(gate);auto found=pages.find(desc.pfspara);destroy_subpage=found!=pages.end()&&found->second->subpage&&found->second->context==context;}
    if(desc.pfspara&&(desc.idobj==0||destroy_subpage))reinterpret_cast<PtsObjDestroyPara>(context->installed->methods[desc.idobj].pfnDestroyPara)(desc.pfspara);
    text.reset();floater.reset();
    if(desc.pfsparaclient)reinterpret_cast<PtsDestroyParaclient>(context->info.fscbk.cbkgen.pfnDestroyParaclient)(context->info.pfsclient,desc.pfsparaclient);
}
// A managed object callback may successfully format a native subtrack and then
// throw, resetting its out handle. Track those exports across callback reentry
// so the enclosing failure still releases the now-unreported native resources.
static thread_local std::vector<std::shared_ptr<TrackData>>* callback_tracks=nullptr;
static thread_local std::vector<std::shared_ptr<BreakData>>* callback_records=nullptr;
static thread_local std::vector<std::shared_ptr<PageData>>* callback_pages=nullptr;
void unregister_page(const std::shared_ptr<PageData>& page);
struct CallbackTracks {
    std::vector<std::shared_ptr<TrackData>> created;
    std::vector<std::shared_ptr<TrackData>>* previous;
    void* claimed=nullptr;
    std::vector<std::shared_ptr<BreakData>> created_records;
    void* claimed_record=nullptr;
    std::vector<std::shared_ptr<BreakData>>* previous_records;
    std::vector<std::shared_ptr<PageData>> created_pages;
    std::vector<std::shared_ptr<PageData>>* previous_pages;
    CallbackTracks():previous(callback_tracks),previous_records(callback_records),previous_pages(callback_pages){callback_tracks=&created;callback_records=&created_records;callback_pages=&created_pages;}
    ~CallbackTracks(){
        callback_tracks=previous;callback_records=previous_records;callback_pages=previous_pages;
        for(auto& record:created_records)if(record.get()!=claimed_record){
            {std::lock_guard<std::mutex> lock(gate);records.erase(record.get());}
            record.reset();
        }
        for(auto& track:created)if(track.get()!=claimed){
            {std::lock_guard<std::mutex> lock(gate);tracks.erase(track.get());}
            track.reset();
        }
        for(auto& page:created_pages)if(page.get()!=claimed){unregister_page(page);page.reset();}
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

int format_text(const std::shared_ptr<ParaData>& para,uint32_t direction,const FSRECT& rect,
    const std::shared_ptr<Continuation>& input,std::shared_ptr<Continuation>& output,bool bottomless,bool force,bool keep_self=false) {
    auto context=para->context;auto& cb=context->info.fscbk.cbktxt;void* client=context->info.pfsclient;
    if(!cb.pfnGetTextProperties||!cb.pfnFormatLine||!cb.pfnDestroyLine||!cb.pfnDestroyLineBreakRecord)return PTS_tserrInvalidParameter;
    FSTXTPROPS props{};int status=reinterpret_cast<PtsGetTextProperties>(cb.pfnGetTextProperties)(client,para->desc.nmp,0,&props);
    if(status)return status;
    if(keep_self)props.fKeepTogether=1;
    if(props.fswdir!=direction||props.fDropCap||props.fVerticalGrid||props.fOptimizeParagraph)return PTS_tserrNotImplemented;
    auto text=std::make_shared<TextData>(context);text->direction=direction;para->text=text;
    int dcp=input?input->dcp:props.dcpStartContent,used=0,fit=0;bool overflow=false,ended=false;
    std::shared_ptr<LineBreakData> previous=input?input->line_break:nullptr;text->incoming=previous;
    while(true) {
        text->lines.emplace_back();text->breaks.push_back(std::make_shared<LineBreakData>(context));
        auto& line=text->lines.back();auto& br=text->breaks.back();
        line.dcpFirst=dcp;line.urStart=rect.u;line.dur=rect.du;line.vrStart=rect.v+used;
        line.pfsbreakreclineclient=previous?previous->handle:nullptr;line.fTreatedAsFirst=(dcp==props.dcpStartContent);
        int consumed=0,dependent=0,reformat=0;FSFLRES result{};
        status=reinterpret_cast<PtsFormatLine>(cb.pfnFormatLine)(client,para->desc.pfsparaclient,para->desc.nmp,0,dcp,line.pfsbreakreclineclient,direction,
            rect.u,rect.du,rect.u,rect.du,rect.u,0,0,0,line.fTreatedAsFirst,0,used==0,
            &line.pfslineclient,&consumed,&br->handle,&line.fForceBroken,&result,&line.dvrAscent,&line.dvrDescent,&line.urBBox,&line.durBBox,&dependent,&reformat);
        if(status)return status;
        if(!line.pfslineclient||consumed<=0||line.dvrAscent<0||line.dvrDescent<0||line.durBBox<0||int64_t(line.urBBox)+line.durBBox>INT32_MAX||int64_t(dcp)+consumed>INT32_MAX)return PTS_tserrInvalidClientOutput;
        int64_t height=int64_t(line.dvrAscent)+line.dvrDescent;
        // Structural-marker lines can consume source without visible height.
        if(height<0||height>INT32_MAX-int64_t(rect.v)-used)return PTS_tserrInvalidGeometry;
        line.dcpLim=dcp+consumed;dcp=line.dcpLim;previous=br;
        if(!overflow && height<=int64_t(rect.dv)-used)++fit;else overflow=true;
        used+=int(height);
        ended=result>=fsflrEndOfParagraph&&result<=fsflrEndOfParagraphClearBoth;
        if(!ended&&result!=fsflrOutOfSpace&&result!=fsflrSoftBreak)return PTS_tserrNotImplemented;
        if(ended || (!bottomless&&overflow&&int(text->lines.size())-fit>=std::max(1,props.cMinLinesAfterBreak)))break;
    }
    if(!bottomless && fit<int(text->lines.size())) {
        // Lookahead is enough to preserve widow/orphan policy, without retaining
        // speculative client lines in the formatted page or consuming their text.
        int fitting=fit;
        if(ended && int(text->lines.size())-fit<props.cMinLinesAfterBreak)
            fit=std::max(0,int(text->lines.size())-props.cMinLinesAfterBreak);
        if(fit<props.cMinLinesBeforeBreak)fit=0;
        if(!fit&&force)fit=fitting;
        if(!fit&&force&&!text->lines.empty()){fit=1;para->forced_progress=true;}
        if(force&&fit<int(text->lines.size())) {
            int64_t prefix_height=0;for(int index=0;index<fit;++index)prefix_height+=text->lines[index].dvrAscent+text->lines[index].dvrDescent;
            // A structural-only prefix must not use up the forced-progress
            // opportunity before the first real oversized line is accepted.
            if(!prefix_height){++fit;para->forced_progress=true;}
        }
        // A new keep-together paragraph that does not fit the remaining area
        // is deferred intact. At a fresh area, an oversized paragraph must
        // still split so pagination makes progress rather than looping.
        if(props.fKeepTogether&&!force&&!input)fit=0;
        if(fit<int(text->lines.size())||!ended) {
            output=std::make_shared<Continuation>();output->context=context;output->paragraph=para->desc.nmp;
            output->dcp=fit?text->lines[fit-1].dcpLim:(input?input->dcp:props.dcpStartContent);
            output->line_break=fit?text->breaks[fit-1]:text->incoming;
        }
        for(size_t i=fit;i<text->lines.size();++i)if(text->lines[i].pfslineclient)reinterpret_cast<PtsDestroyLine>(cb.pfnDestroyLine)(client,text->lines[i].pfslineclient);
        text->lines.resize(fit);text->breaks.resize(fit);
    }
    used=0;
    for(const auto& line:text->lines) {
        int height=line.dvrAscent+line.dvrDescent;used+=height;
        FSBBOX bbox{1,{line.urBBox,line.vrStart,line.durBBox,height}};status=merge_bbox(para->desc.fsbbox,bbox);if(status)return status;
    }
    para->desc.pfspara=text.get();para->desc.dvrUsed=para->forced_progress?std::min(used,rect.dv):used;
    {std::lock_guard<std::mutex> lock(gate);texts[text.get()]=text;}
    return 0;
}

int format_block_ui(const std::shared_ptr<ParaData>& para,uint32_t direction,const FSRECT& rect,
    void* input_margin,bool bottomless,bool empty_ok,int suppress,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA suppress_break,
    std::shared_ptr<Continuation>& output,void** margin_output) {
    auto context=para->context;auto& obj=context->info.fscbk.cbkobj;void* client=context->info.pfsclient;
    if(!obj.pfnGetObjectHandlerInfo||!obj.pfnConvertMcsclient)return PTS_tserrInvalidParameter;
    FloaterHandler handler{};
    int status=reinterpret_cast<PtsGetObjectHandlerInfo>(obj.pfnGetObjectHandlerInfo)(client,2,&handler);if(status)return status;
    if(handler.magic!=floater_magic)return PTS_tserrInvalidClientOutput;
    auto& cb=handler.init.fsfloatercbk;
    if(!cb.pfnGetFloaterProperties||!cb.pfnDestroyFloaterContent||!cb.pfnDestroyFloaterContentBreakRecord||!cb.pfnGetMCSClientAfterFloater||
       !(bottomless?cb.pfnFormatFloaterContentBottomless:cb.pfnFormatFloaterContentFinite))return PTS_tserrInvalidParameter;
    FSFLOATERPROPS props{};
    status=reinterpret_cast<PtsGetFloaterProperties>(cb.pfnGetFloaterProperties)(client,para->desc.nmp,direction,&props);if(status)return status;
    // A nonfloating block owns the entire column width. Floating obstacles
    // need a separate text-flow algorithm, rather than treating them as blocks.
    if(props.fFloat||props.fskwr!=fskwrNone||props.fskclear!=fskclearNone)return PTS_tserrNotImplemented;
    int top=0;
    status=reinterpret_cast<PtsConvertMcsclient>(obj.pfnConvertMcsclient)(client,para->desc.pfsparaclient,para->desc.nmp,direction,input_margin,suppress,&top);if(status)return status;
    if(int64_t(rect.v)+top<INT32_MIN||int64_t(rect.v)+top>INT32_MAX||int64_t(rect.dv)-top>INT32_MAX)return PTS_tserrInvalidGeometry;
    auto data=std::make_shared<FloaterData>(context);data->callbacks=cb;para->floater=data;
    auto& details=data->details;details.fsnmFloater=para->desc.nmp;details.fskupdContent=fskupdNew;
    int width=0,height=0,polygons=0,vertices=0;FSBBOX box{};
    int available=std::max(0,rect.dv-top);FSFMTR finite{};
    if(bottomless) {
        FSFMTRBL result{};
        status=reinterpret_cast<PtsFormatFloaterContentBottomless>(cb.pfnFormatFloaterContentBottomless)(client,para->desc.pfsparaclient,para->desc.nmp,suppress,direction,1,rect.du,available,
            &result,&details.pfsFloaterContent,&width,&height,&box,&polygons,&vertices);
        if(!status&&result!=fmtrblGoalReached)status=PTS_tserrNotImplemented;
    } else {
        status=reinterpret_cast<PtsFormatFloaterContentFinite>(cb.pfnFormatFloaterContentFinite)(client,para->desc.pfsparaclient,nullptr,0,para->desc.nmp,nullptr,empty_ok,suppress,direction,1,rect.du,available,suppress_break,
            &finite,&details.pfsFloaterContent,&data->break_record,&width,&height,&box,&polygons,&vertices);
    }
    if(status)return status;
    if(polygons||vertices||data->break_record)return PTS_tserrNotImplemented;
    if(!bottomless&&finite.kstop==fmtrNoProgressOutOfSpace) {
        if(details.pfsFloaterContent||width||height)return PTS_tserrInvalidClientOutput;
        output=std::make_shared<Continuation>();output->context=context;output->paragraph=para->desc.nmp;
        return 0;
    }
    if(!bottomless&&finite.kstop!=fmtrGoalReached)return PTS_tserrNotImplemented;
    if(width<0||height<0||width>rect.du||int64_t(rect.v)+top+height>INT32_MAX)return PTS_tserrInvalidClientOutput;
    if(!bottomless&&int64_t(top)+height>rect.dv&&!finite.fForcedProgress)return PTS_tserrInvalidClientOutput;
    details.fsrcFloater={rect.u,rect.v+top,width,height};
    if(box.fDefined) {
        if(int64_t(box.fsrc.u)+rect.u<INT32_MIN||int64_t(box.fsrc.u)+rect.u>INT32_MAX||
           int64_t(box.fsrc.v)+rect.v+top<INT32_MIN||int64_t(box.fsrc.v)+rect.v+top>INT32_MAX)return PTS_tserrInvalidClientOutput;
        box.fsrc.u+=rect.u;box.fsrc.v+=rect.v+top;
    }
    para->desc.fsbbox=box;para->desc.pfspara=data.get();para->desc.dvrTopSpace=top;
    para->forced_progress=!bottomless&&finite.fForcedProgress;
    para->desc.dvrUsed=int(std::max<int64_t>(0,bottomless?int64_t(top)+height:std::min<int64_t>(int64_t(top)+height,rect.dv)));
    status=reinterpret_cast<PtsGetMCSClientAfterFloater>(cb.pfnGetMCSClientAfterFloater)(client,para->desc.pfsparaclient,para->desc.nmp,direction,input_margin,margin_output);if(status)return status;
    {std::lock_guard<std::mutex> lock(gate);floaters[data.get()]=data;}
    return 0;
}

int format_track(const std::shared_ptr<Context>& context,void* segment,uint32_t direction,const FSRECT& rect,
    int suppress,void* incoming,std::shared_ptr<TrackData>& result,void** margin_output,bool bottomless=false,const std::shared_ptr<Continuation>& input={},bool empty_ok=false,bool previous_page=true,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA suppress_break=fsksuppresshardbreakbeforefirstparaPageAndColumn,bool keep_self=false) {
    auto& cb=context->info.fscbk.cbkgen;void* client=context->info.pfsclient;
    if(!segment||direction!=PTS_fswdirES||rect.du<=0||rect.dv<0||int64_t(rect.u)+rect.du>INT32_MAX||int64_t(rect.v)+rect.dv>INT32_MAX)return PTS_tserrNotImplemented;
    if(!cb.pfnGetFirstPara||!cb.pfnGetNextPara||!cb.pfnGetParaProperties||!cb.pfnCreateParaclient||!cb.pfnDestroyParaclient||!context->info.fscbk.cbkobj.pfnDestroyMcsclient)return PTS_tserrInvalidParameter;
    auto track=std::make_shared<TrackData>(context);track->segment=segment;track->rect=rect;track->bottomless=bottomless;
    int found=0,status=0;void* name=nullptr;
    if(input){if(input->segment!=segment)return PTS_tserrInvalidBreakRecord;name=input->paragraph;found=1;}
    else {status=reinterpret_cast<PtsGetFirstPara>(cb.pfnGetFirstPara)(client,segment,&found,&name);if(status)return status;}
    static thread_local std::vector<std::pair<void*,void*>> active;
    const auto key=std::make_pair(static_cast<void*>(context.get()),segment);
    if(active.size()>=128 || std::find(active.begin(),active.end(),key)!=active.end())return PTS_tserrInvalidClientOutput;
    active.push_back(key);
    struct Pop {std::vector<std::pair<void*,void*>>& value;~Pop(){value.pop_back();}} pop{active};
    std::unordered_set<void*> visited;MarginOwner margin{context};void* input_margin=incoming;
    int chain_start=-1;MarginOwner chain_margin{context};std::shared_ptr<Continuation> chain_position;
    while(found) {
        if(!name||!visited.insert(name).second)return PTS_tserrInvalidClientOutput;
        FSPAP props{};status=reinterpret_cast<PtsGetParaProperties>(cb.pfnGetParaProperties)(client,name,&props);if(status)return status;
        if(props.idobj!=-1&&props.idobj!=0&&props.idobj!=1&&props.idobj!=2)return PTS_tserrNotImplemented;
        bool first=track->paras.empty();
        bool continued=input&&first&&(input->child||input->dcp>0);
        bool page_break=!bottomless&&!continued&&props.fBreakPageBefore&&!(first&&suppress_break==fsksuppresshardbreakbeforefirstparaPageAndColumn);
        bool column_break=!bottomless&&!continued&&props.fBreakColumnBefore&&!(first&&suppress_break!=fsksuppresshardbreakbeforefirstparaNone);
        if(page_break||column_break) {
            auto position=std::make_shared<Continuation>();position->context=context;position->segment=segment;position->paragraph=name;
            track->continuation=position;
            track->stop=page_break?(first?fmtrNoProgressPageBreakBeforePara:fmtrBrokenPageBreakBeforePara):(first?fmtrNoProgressColumnBreakBeforePara:fmtrBrokenColumnBreakBeforePara);
            break;
        }
        if(!bottomless&&props.fKeepWithNext&&chain_start<0) {
            chain_start=int(track->paras.size());
            if(input&&chain_start==0)chain_position=input;
            else {chain_position=std::make_shared<Continuation>();chain_position->context=context;chain_position->segment=segment;chain_position->paragraph=name;}
            chain_margin.reset();
            if(input_margin) {
                auto duplicate=context->info.fscbk.cbkobj.pfnDuplicateMcsclient;if(!duplicate)return PTS_tserrInvalidParameter;
                status=reinterpret_cast<PtsDuplicateMcsclient>(duplicate)(client,input_margin,&chain_margin.value);if(status)return status;
                if(!chain_margin.value)return PTS_tserrInvalidClientOutput;
            }
        }
        auto para=std::make_shared<ParaData>();para->context=context;para->desc.nmp=name;para->desc.idobj=props.idobj;para->desc.fsupdinf.fskupd=fskupdNew;
        status=reinterpret_cast<PtsCreateParaclient>(cb.pfnCreateParaclient)(client,name,&para->desc.pfsparaclient);if(status)return status;
        if(!para->desc.pfsparaclient)return PTS_tserrInvalidClientOutput;
        FSRECT available=rect;available.v+=track->used;available.dv-=track->used;
        std::shared_ptr<Continuation> next_position;FSFMTRKSTOP next_stop=fmtrBrokenOutOfSpace;
        auto resume=(input&&track->paras.empty())?input:std::shared_ptr<Continuation>{};
        if(props.idobj==-1)status=format_text(para,direction,available,resume,next_position,bottomless,!empty_ok&&track->paras.empty(),keep_self||props.fKeepWithNext);
        else if(props.idobj==2) {
            void* margin_next=nullptr;
            status=format_block_ui(para,direction,available,input_margin,bottomless,empty_ok||!track->paras.empty(),suppress&&first,first?suppress_break:fsksuppresshardbreakbeforefirstparaNone,next_position,&margin_next);
            // Deferral preserves the incoming margin. Accepted content returns
            // a newly owned bottom-margin state through the original callback.
            if(!next_position){margin.reset();margin.value=margin_next;input_margin=margin.value;}
            else if(margin_next)reinterpret_cast<PtsDestroyMcsclient>(context->info.fscbk.cbkobj.pfnDestroyMcsclient)(client,margin_next);
        } else {
            auto& methods=context->installed->methods[props.idobj];
            if(!(bottomless?methods.pfnFormatParaBottomless:methods.pfnFormatParaFinite)||!methods.pfnDestroyPara)return PTS_tserrInvalidParameter;
            CallbackTracks pending;
            void* margin_next=nullptr;FSKCLEAR clear{};int can_break=0;
            if(bottomless) {
                FSFMTRBL formatted{};
                status=reinterpret_cast<PtsObjFormatParaBottomless>(methods.pfnFormatParaBottomless)(context->object_contexts[props.idobj],para->desc.pfsparaclient,name,0,nullptr,
                    suppress&&track->paras.empty(),direction,available.u,available.du,available.v,input_margin,fskclearNone,0,
                    &formatted,&para->desc.pfspara,&para->desc.dvrUsed,&para->desc.fsbbox,&margin_next,&clear,&para->desc.dvrTopSpace,&can_break);
                if(!status&&formatted!=fmtrblGoalReached)status=PTS_tserrNotImplemented;
            } else {
                FSFMTR fmtr{};void* break_output=nullptr;
                void* child_input=resume&&resume->child?resume->child.get():nullptr;
                status=reinterpret_cast<PtsObjFormatParaFinite>(methods.pfnFormatParaFinite)(context->object_contexts[props.idobj],para->desc.pfsparaclient,child_input,child_input&&previous_page?1:0,name,0,nullptr,nullptr,
                    (empty_ok||!track->paras.empty())?1:0,suppress&&track->paras.empty(),direction,&available,input_margin,fskclearNone,first?suppress_break:fsksuppresshardbreakbeforefirstparaNone,0,
                    &fmtr,&para->desc.pfspara,&break_output,&para->desc.dvrUsed,&para->desc.fsbbox,&margin_next,&clear,&para->desc.dvrTopSpace,&can_break);
                para->forced_progress=fmtr.fForcedProgress!=0;
                pending.claimed_record=status?nullptr:break_output;
                if(!status && break_output) {
                    auto child=lookup_owned(records,context.get(),break_output);
                    if(!child||child->page||child->subpage!=(props.idobj==1))return PTS_tserrInvalidBreakRecord;
                    next_position=std::make_shared<Continuation>();next_position->context=context;next_position->paragraph=name;next_position->child=child;next_position->child_object=props.idobj;next_stop=fmtr.kstop;
                    if(fmtr.kstop>=fmtrNoProgressOutOfSpace){if(resume)next_position=resume;else next_position->child.reset();pending.claimed_record=nullptr;}
                } else if(!status&&fmtr.kstop!=fmtrGoalReached)status=PTS_tserrNotImplemented;
            }
            pending.claimed=para->desc.pfspara;
            margin.reset();margin.value=margin_next;input_margin=margin.value;
            if(!status&&clear!=fskclearNone)status=PTS_tserrNotImplemented;
        }
        if(status)return status;
        if(para->forced_progress&&para->desc.dvrUsed>available.dv)para->desc.dvrUsed=available.dv;
        if(para->desc.dvrUsed<0||para->desc.dvrUsed>available.dv)return PTS_tserrInvalidClientOutput;
        if(!bottomless&&next_position&&para->desc.dvrUsed==0&&chain_start>=0&&(chain_start>0||empty_ok)) {
            // None of the following paragraph fits. Remove accepted members
            // of the keep chain and resume from its original immutable start.
            track->paras.resize(chain_start);track->used=0;track->bbox={};
            for(const auto& accepted:track->paras){track->used+=accepted->desc.dvrUsed;status=merge_bbox(track->bbox,accepted->desc.fsbbox);if(status)return status;}
            margin.reset();margin.value=chain_margin.value;chain_margin.value=nullptr;
            track->continuation=chain_position;track->stop=track->paras.empty()?fmtrNoProgressOutOfSpace:fmtrBrokenOutOfSpace;break;
        }
        bool source_progress=para->text&&!para->text->lines.empty();
        if(props.idobj==0&&para->desc.pfspara){auto child=lookup_owned(tracks,context.get(),para->desc.pfspara);source_progress=child&&!child->paras.empty();}
        if(props.idobj==1&&para->desc.pfspara){auto child=lookup_owned(pages,context.get(),para->desc.pfspara);if(child){source_progress=!child->track->paras.empty();if(child->section)for(const auto& column:child->section->columns)source_progress=source_progress||!column->paras.empty();}}
        if(para->desc.dvrUsed>0||!next_position||para->forced_progress||source_progress){track->forced_progress=track->forced_progress||para->forced_progress;track->used+=para->desc.dvrUsed;status=merge_bbox(track->bbox,para->desc.fsbbox);if(status)return status;track->paras.push_back(para);}
        if(next_position){next_position->segment=segment;track->continuation=next_position;if(next_stop==fmtrBrokenPageBreakBeforePara||next_stop==fmtrNoProgressPageBreakBeforePara)track->stop=track->paras.empty()?fmtrNoProgressPageBreakBeforePara:fmtrBrokenPageBreakBeforePara;
            else if(next_stop==fmtrBrokenColumnBreakBeforePara||next_stop==fmtrNoProgressColumnBreakBeforePara)track->stop=track->paras.empty()?fmtrNoProgressColumnBreakBeforePara:fmtrBrokenColumnBreakBeforePara;
            else track->stop=track->paras.empty()?fmtrNoProgressOutOfSpace:fmtrBrokenOutOfSpace;break;}
        if(!props.fKeepWithNext){chain_start=-1;chain_position.reset();chain_margin.reset();}
        void* next=nullptr;status=reinterpret_cast<PtsGetNextPara>(cb.pfnGetNextPara)(client,segment,name,&found,&next);if(status)return status;name=next;
    }
    if(bottomless)track->rect.dv=track->used;
    result=track;*margin_output=margin.value;margin.value=nullptr;return 0;
}
void clear_subpage(const std::shared_ptr<PageData>& page);
void clear_track(const std::shared_ptr<TrackData>& track) {
    track->update=fskupdNoChange;
    for(const auto& para:track->paras) {
        para->desc.fsupdinf.fskupd=fskupdNoChange;
        if(para->floater)para->floater->details.fskupdContent=fskupdNoChange;
        if(para->desc.idobj==0){auto child=lookup_owned(tracks,track->context.get(),para->desc.pfspara);if(child)clear_track(child);}
        if(para->desc.idobj==1){auto child=lookup_owned(pages,track->context.get(),para->desc.pfspara);if(child&&child->subpage)clear_subpage(child);}
    }
}
}
namespace {
std::shared_ptr<BreakData> make_record(const std::shared_ptr<Context>& context,const std::shared_ptr<Continuation>& position,bool page,void* section=nullptr,bool subpage=false) {
    auto record=std::make_shared<BreakData>(context);record->position=position;record->page=page;record->section=section;record->subpage=subpage;
    if(!page&&callback_records)callback_records->push_back(record);
    {std::lock_guard<std::mutex> lock(gate);records.emplace(record.get(),record);}
    return record;
}
int destroy_record(void* context,void* handle,bool page,bool subpage=false) {
    std::shared_ptr<BreakData> record;
    {std::lock_guard<std::mutex> lock(gate);auto found=records.find(handle);
        if(found==records.end()||found->second->context.get()!=context||found->second->page!=page||found->second->subpage!=subpage||contexts.find(context)==contexts.end())return PTS_tserrInvalidBreakRecord;
        record=found->second;records.erase(found);}
    return 0;
}
}
extern "C" __declspec(dllexport) int WINAPI FsDestroyPageBreakRecord(void* context,void* handle){return destroy_record(context,handle,true);}
extern "C" __declspec(dllexport) int WINAPI FsDestroySubtrackBreakRecord(void* context,void* handle){return destroy_record(context,handle,false);}
extern "C" __declspec(dllexport) int WINAPI FsDuplicateSubtrackBreakRecord(void* context,void* handle,void** output) {
    if(!output)return PTS_tserrNullOutputParameter;*output=nullptr;
    auto original=lookup_owned(records,context,handle);if(!original||original->page||original->subpage)return PTS_tserrInvalidBreakRecord;
    try {auto record=make_record(original->context,original->position,false);*output=record.get();return 0;}
    catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsFormatSubtrackFinite(void* context_handle,void* break_input,int previous_page,void* segment,int area,
    void* footnote,void* geometry,int empty_ok,int suppress,uint32_t direction,const FSRECT* rect,void* margin_input,
    FSKCLEAR clear,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA suppress_break,FSFMTR* formatted,void** output,void** break_output,
    int* used,FSBBOX* bbox,void** margin_output,FSKCLEAR* clear_output,int* top_space) {
    if(!formatted||!output||!break_output||!used||!bbox||!margin_output||!clear_output||!top_space)return PTS_tserrNullOutputParameter;
    *formatted={};*output=*break_output=*margin_output=nullptr;*used=*top_space=0;*bbox={};*clear_output=fskclearNone;
    auto context=find_context(context_handle);if(!context)return PTS_tserrInvalidPtsContext;
    if(!rect)return PTS_tserrInvalidParameter;
    if(area||footnote||geometry||clear!=fskclearNone)return PTS_tserrNotImplemented;
    std::shared_ptr<BreakData> input;
    if(break_input){input=lookup_owned(records,context_handle,break_input);if(!input||input->page||input->subpage)return PTS_tserrInvalidBreakRecord;}
    try {
        std::shared_ptr<TrackData> track;MarginOwner margin{context};
        FSPAP owner{};auto properties=context->info.fscbk.cbkgen.pfnGetParaProperties;if(!properties)return PTS_tserrInvalidParameter;
        int status=reinterpret_cast<PtsGetParaProperties>(properties)(context->info.pfsclient,segment,&owner);if(status)return status;
        status=format_track(context,segment,direction,*rect,suppress,margin_input,track,&margin.value,false,input?input->position:nullptr,empty_ok!=0,previous_page!=0,suppress_break,owner.fKeepWithNext!=0);if(status)return status;
        track->subtrack=true;
        if(callback_tracks)callback_tracks->push_back(track);
        {std::lock_guard<std::mutex> lock(gate);tracks.emplace(track.get(),track);}
        std::shared_ptr<BreakData> outgoing;
        try {if(track->continuation)outgoing=make_record(context,track->continuation,false);}
        catch(...){{std::lock_guard<std::mutex> lock(gate);tracks.erase(track.get());}throw;}
        *output=track.get();*used=track->used;*bbox=track->bbox;*margin_output=margin.value;margin.value=nullptr;formatted->kstop=track->stop;formatted->fForcedProgress=track->forced_progress;*break_output=outgoing?outgoing.get():nullptr;return 0;
    }catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsDestroySubtrack(void* context_handle,void* handle) {
    std::shared_ptr<TrackData> track;
    {std::lock_guard<std::mutex> lock(gate);auto found=tracks.find(handle);
        if(found==tracks.end()||!found->second->subtrack||found->second->context.get()!=context_handle||contexts.find(context_handle)==contexts.end())return PTS_tserrInvalidQuery;
        track=found->second;tracks.erase(found);}
    return 0;
}
namespace {
int build_page(const std::shared_ptr<Context>& context,void* section,bool bottomless,std::shared_ptr<PageData>& result,const std::shared_ptr<Continuation>& input={}) {
    auto& cb=context->info.fscbk.cbkgen;void* client=context->info.pfsclient;
    if(!section||!cb.pfnGetPageDimensions||!cb.pfnGetSectionProperties||!cb.pfnGetMainTextSegment)return PTS_tserrInvalidParameter;
    FSRECT rect{};uint32_t direction=0,section_direction=0;int header=0,width=0,height=0;
    int status=reinterpret_cast<PtsGetPageDimensions>(cb.pfnGetPageDimensions)(client,section,&direction,&header,&width,&height,&rect);if(status)return status;
    int new_page=0,balance=0,columns=0,segments=0,heights=0;
    status=reinterpret_cast<PtsGetSectionProperties>(cb.pfnGetSectionProperties)(client,section,&new_page,&section_direction,&balance,&columns,&segments,&heights);if(status)return status;
    if(direction!=PTS_fswdirES||section_direction!=direction||header||columns<1||segments||heights||balance|| (bottomless&&columns!=1))return PTS_tserrNotImplemented;
    if(width<=0||(!bottomless&&height<=0)||rect.u<0||rect.v<0||rect.du<=0||int64_t(rect.u)+rect.du>width||(!bottomless&&int64_t(rect.v)+rect.dv>height))return PTS_tserrInvalidGeometry;
    if(bottomless)rect.dv=INT32_MAX-rect.v;
    void* segment=nullptr;status=reinterpret_cast<PtsGetMainTextSegment>(cb.pfnGetMainTextSegment)(client,section,&segment);if(status)return status;
    auto page=std::make_shared<PageData>(context);page->bottomless=bottomless;MarginOwner margin{context};
    auto& details=page->details;details.fskupd=fskupdNew;
    if(columns==1) {
        status=format_track(context,segment,direction,rect,1,nullptr,page->track,&margin.value,bottomless,input);if(status)return status;
        details.fSimple=1;page->continuation=page->track->continuation;page->stop=page->track->stop;page->forced_progress=page->track->forced_progress;
        auto& desc=details.u.simple.trackdescr;desc.fsupdinf.fskupd=fskupdNew;desc.nms=segment;desc.fsrc=page->track->rect;desc.fsbbox=page->track->bbox;desc.pfstrack=page->track.get();
    } else {
        if(!cb.pfnGetSectionColumnInfo)return PTS_tserrInvalidParameter;
        std::vector<FSCOLUMNINFO> info(columns);int actual=0;
        status=reinterpret_cast<PtsGetSectionColumnInfo>(cb.pfnGetSectionColumnInfo)(client,section,direction,columns,info.data(),&actual);if(status)return status;
        if(actual!=columns)return PTS_tserrInvalidClientOutput;
        // Validate the whole column geometry before invoking callbacks that
        // create managed paragraph clients or native continuation resources.
        int64_t u=rect.u;
        for(const auto& col:info){if(col.durBefore<0||col.durWidth<=0)return PTS_tserrInvalidClientOutput;u+=int64_t(col.durBefore)+col.durWidth;if(u>int64_t(rect.u)+rect.du)return PTS_tserrInvalidGeometry;}
        auto data=std::make_shared<SectionData>(context);page->section=data;
        auto& desc=data->description;desc.fsupdinf.fskupd=fskupdNew;desc.nms=section;desc.fsrc=rect;desc.pfssection=data.get();
        auto continuation=input;bool finished=false,stop_page=false;FSFMTRKSTOP stop=fmtrGoalReached;u=rect.u;
        for(int index=0;index<columns;++index) {
            u+=info[index].durBefore;FSRECT column{int(u),rect.v,info[index].durWidth,rect.dv};u+=column.du;
            std::shared_ptr<TrackData> track;MarginOwner column_margin{context};
            if(finished||stop_page){track=std::make_shared<TrackData>(context);track->segment=segment;track->rect=column;}
            else {
                status=format_track(context,segment,direction,column,1,nullptr,track,&column_margin.value,false,continuation,false,index==0,index==0?fsksuppresshardbreakbeforefirstparaPageAndColumn:fsksuppresshardbreakbeforefirstparaColumn);if(status)return status;
                continuation=track->continuation;finished=!continuation;stop=track->stop;
                stop_page=stop==fmtrBrokenPageBreakBeforePara||stop==fmtrNoProgressPageBreakBeforePara;
                if(track->stop==fmtrNoProgressOutOfSpace)return PTS_tserrNotImplemented;
            }
            page->forced_progress=page->forced_progress||track->forced_progress;status=merge_bbox(desc.fsbbox,track->bbox);if(status)return status;data->columns.push_back(track);
        }
        page->track=data->columns.back();page->continuation=continuation;page->stop=stop;
        details.fSimple=0;auto& body=details.u.complex;body.fswdirPageProper=direction;body.fsupdinfPageBody.fskupd=fskupdNew;
        body.fsrcPageBody=rect;body.fsrcPageMarginActual=rect;body.fsbboxPageBody=desc.fsbbox;body.cSections=1;
    }
    result=page;return 0;
}
int register_page(const std::shared_ptr<PageData>& page) {
    std::lock_guard<std::mutex> lock(gate);
    try {
        if(page->section){for(const auto& track:page->section->columns)tracks.emplace(track.get(),track);sections.emplace(page->section.get(),page->section);}
        else tracks.emplace(page->track.get(),page->track);
        pages.emplace(page.get(),page);return 0;
    }catch(...){
        if(page->section){for(const auto& track:page->section->columns)tracks.erase(track.get());sections.erase(page->section.get());}
        else tracks.erase(page->track.get());throw;
    }
}
void unregister_page(const std::shared_ptr<PageData>& page) {
    std::lock_guard<std::mutex> lock(gate);
    if(page->section){for(const auto& track:page->section->columns)tracks.erase(track.get());sections.erase(page->section.get());}
    else tracks.erase(page->track.get());
    pages.erase(page.get());
}
}
extern "C" __declspec(dllexport) int WINAPI FsCreatePageFinite(void* context_handle,void* break_input,void* section,FSFMTR* formatted,void** output,void** break_output) {
    if(!formatted||!output||!break_output)return PTS_tserrNullOutputParameter;
    *formatted={};*output=*break_output=nullptr;
    auto context=find_context(context_handle);if(!context)return PTS_tserrInvalidPtsContext;
    std::shared_ptr<BreakData> input;
    if(break_input){input=lookup_owned(records,context_handle,break_input);if(!input||!input->page)return PTS_tserrInvalidBreakRecord;section=input->section;}
    try {
        std::shared_ptr<PageData> page;int status=build_page(context,section,false,page,input?input->position:nullptr);if(status)return status;
        if(page->stop==fmtrNoProgressOutOfSpace)return PTS_tserrNotImplemented;
        std::shared_ptr<BreakData> outgoing;if(page->continuation)outgoing=make_record(context,page->continuation,true,section);
        try {register_page(page);}catch(...){if(outgoing)destroy_record(context_handle,outgoing.get(),true);throw;}
        formatted->kstop=page->stop;formatted->fForcedProgress=page->forced_progress;*output=page.get();*break_output=outgoing?outgoing.get():nullptr;return 0;
    }catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsCreatePageBottomless(void* context_handle,void* section,FSFMTRBL* formatted,void** output) {
    if(!formatted||!output)return PTS_tserrNullOutputParameter;*formatted=fmtrblGoalReached;*output=nullptr;
    auto context=find_context(context_handle);if(!context)return PTS_tserrInvalidPtsContext;
    try {
        std::shared_ptr<PageData> page;int status=build_page(context,section,true,page);if(status)return status;
        register_page(page);*output=page.get();return 0;
    }catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsFormatSubtrackBottomless(void* context_handle,void* segment,int area,void* geometry,int suppress,uint32_t direction,
    int u,int width,int v,void* margin_input,FSKCLEAR clear,int interruptible,FSFMTRBL* formatted,void** output,int* used,FSBBOX* bbox,
    void** margin_output,FSKCLEAR* clear_output,int* top_space,int* can_interrupt) {
    if(!formatted||!output||!used||!bbox||!margin_output||!clear_output||!top_space||!can_interrupt)return PTS_tserrNullOutputParameter;
    *formatted=fmtrblGoalReached;*output=*margin_output=nullptr;*used=*top_space=*can_interrupt=0;*bbox={};*clear_output=fskclearNone;
    auto context=find_context(context_handle);if(!context)return PTS_tserrInvalidPtsContext;
    if(v<0||area||geometry||clear!=fskclearNone)return PTS_tserrNotImplemented;
    try {
        FSRECT rect{u,v,width,INT32_MAX-v};std::shared_ptr<TrackData> track;MarginOwner margin{context};
        int status=format_track(context,segment,direction,rect,suppress,margin_input,track,&margin.value,true);if(status)return status;
        track->subtrack=true;if(callback_tracks)callback_tracks->push_back(track);
        {std::lock_guard<std::mutex> lock(gate);tracks.emplace(track.get(),track);}
        *output=track.get();*used=track->used;*bbox=track->bbox;*margin_output=margin.value;margin.value=nullptr;return 0;
    }catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsUpdateBottomlessSubtrack(void* context_handle,void* handle,void* segment,int area,void* geometry,int suppress,uint32_t direction,
    int u,int width,int v,void* margin_input,FSKCLEAR clear,int interruptible,FSFMTRBL* formatted,int* used,FSBBOX* bbox,
    void** margin_output,FSKCLEAR* clear_output,int* top_space,int* uninterruptible) {
    if(!formatted||!used||!bbox||!margin_output||!clear_output||!top_space||!uninterruptible)return PTS_tserrNullOutputParameter;
    *formatted=fmtrblGoalReached;*margin_output=nullptr;*used=*top_space=*uninterruptible=0;*bbox={};*clear_output=fskclearNone;
    auto track=lookup_owned(tracks,context_handle,handle);
    if(!track||!track->subtrack||!track->bottomless||track->segment!=segment)return PTS_tserrInvalidQuery;
    if(area||geometry||clear!=fskclearNone)return PTS_tserrNotImplemented;if(v<0)return PTS_tserrInvalidGeometry;
    try {
        FSRECT rect{u,v,width,INT32_MAX-v};std::shared_ptr<TrackData> replacement;MarginOwner margin{track->context};
        int status=format_track(track->context,segment,direction,rect,suppress,margin_input,replacement,&margin.value,true);if(status)return status;
        {
            std::lock_guard<std::mutex> lock(gate);
            // Keep the exported track identity and move the old owned clients
            // into the temporary tree for teardown outside the registry lock.
            std::swap(track->rect,replacement->rect);std::swap(track->bbox,replacement->bbox);
            std::swap(track->used,replacement->used);track->paras.swap(replacement->paras);
            track->continuation.swap(replacement->continuation);
            std::swap(track->stop,replacement->stop);std::swap(track->forced_progress,replacement->forced_progress);
            track->update=fskupdNew;
        }
        *used=track->used;*bbox=track->bbox;*margin_output=margin.value;margin.value=nullptr;*uninterruptible=1;return 0;
    }catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
namespace {
struct TrackShift {
    int delta;
    std::vector<std::pair<int32_t*,int32_t>> values;
    std::vector<std::shared_ptr<TrackData>> owners;
    std::unordered_set<TrackData*> visited;
    int add(int32_t& value,int64_t extent=0) {
        int64_t shifted=int64_t(value)+delta;
        if(extent<0||shifted<INT32_MIN||shifted>INT32_MAX||shifted+extent>INT32_MAX)return PTS_tserrInvalidGeometry;
        values.emplace_back(&value,int32_t(shifted));return 0;
    }
    int box(FSBBOX& value){return value.fDefined?add(value.fsrc.v,value.fsrc.dv):0;}
    int collect(const std::shared_ptr<TrackData>& track) {
        if(!visited.insert(track.get()).second)return PTS_tserrInvalidClientOutput;
        owners.push_back(track);
        int status=add(track->rect.v,track->rect.dv);if(status)return status;
        status=box(track->bbox);if(status)return status;
        for(const auto& para:track->paras) {
            status=box(para->desc.fsbbox);if(status)return status;
            if(para->text)for(auto& line:para->text->lines){status=add(line.vrStart,int64_t(line.dvrAscent)+line.dvrDescent);if(status)return status;}
            if(para->floater){auto& rect=para->floater->details.fsrcFloater;status=add(rect.v,rect.dv);if(status)return status;}
            if(para->desc.idobj==0){auto child=lookup_owned(tracks,track->context.get(),para->desc.pfspara);if(!child)return PTS_tserrInvalidQuery;status=collect(child);if(status)return status;}
            // Subpage content uses its own local coordinate system. Its outer
            // paragraph bbox moves here; its local tracks remain unchanged.
        }
        return 0;
    }
    void apply(){for(const auto& item:values)*item.first=item.second;for(const auto& track:owners){track->update=fskupdNew;for(const auto& para:track->paras){para->desc.fsupdinf.fskupd=fskupdNew;if(para->floater)para->floater->details.fskupdContent=fskupdNew;}}}
};
}
extern "C" __declspec(dllexport) int WINAPI FsSynchronizeBottomlessSubtrack(void* context_handle,void* handle,void* geometry,uint32_t direction,int shift) {
    auto track=lookup_owned(tracks,context_handle,handle);
    if(!track||!track->subtrack||!track->bottomless)return PTS_tserrInvalidQuery;
    if(geometry||direction!=PTS_fswdirES)return PTS_tserrNotImplemented;
    try {TrackShift plan{shift};int status=plan.collect(track);if(status)return status;std::lock_guard<std::mutex> lock(gate);plan.apply();return 0;}
    catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsUpdateBottomlessPage(void* context_handle,void* handle,void* section,FSFMTRBL* formatted) {
    if(!formatted)return PTS_tserrNullOutputParameter;*formatted=fmtrblGoalReached;
    auto page=lookup_owned(pages,context_handle,handle);if(!page||page->subpage||!page->bottomless)return PTS_tserrInvalidQuery;
    try {
        std::shared_ptr<PageData> replacement;int status=build_page(page->context,section,true,replacement);if(status)return status;
        std::shared_ptr<TrackData> previous=page->track;
        {std::lock_guard<std::mutex> lock(gate);
            tracks.emplace(replacement->track.get(),replacement->track);
            tracks.erase(previous.get());page->track=replacement->track;page->details=replacement->details;page->continuation=replacement->continuation;page->stop=replacement->stop;page->forced_progress=replacement->forced_progress;}
        // Retain page identity while replacing layout atomically. The old
        // clients/lines are released outside the registry lock after success.
        return 0;
    }catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsDestroyPage(void* context_handle,void* handle) {
    std::shared_ptr<PageData> page;
    {std::lock_guard<std::mutex> lock(gate);auto found=pages.find(handle);
        if(found==pages.end()||found->second->subpage||found->second->context.get()!=context_handle||contexts.find(context_handle)==contexts.end())return PTS_tserrInvalidQuery;
        page=found->second;pages.erase(found);
        if(page->section){for(const auto& track:page->section->columns)tracks.erase(track.get());}
        else tracks.erase(page->track.get());}
    return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQueryPageDetails(void* context,void* handle,FSPAGEDETAILS* output) {
    if(!output)return PTS_tserrNullOutputParameter;*output={};auto page=lookup_owned(pages,context,handle);
    if(!page||page->subpage)return PTS_tserrInvalidQuery;*output=page->details;return 0;
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
extern "C" __declspec(dllexport) int WINAPI FsQueryFloaterDetails(void* context,void* handle,FSFLOATERDETAILS* output) {
    if(!output)return PTS_tserrNullOutputParameter;*output={};
    std::shared_ptr<FloaterData> data;
    {std::lock_guard<std::mutex> lock(gate);auto found=floaters.find(handle);
        if(found==floaters.end()||contexts.find(context)==contexts.end())return PTS_tserrInvalidQuery;
        data=found->second.lock();}
    if(!data||data->context.get()!=context)return PTS_tserrInvalidQuery;
    *output=data->details;return 0;
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
    auto page=lookup_owned(pages,context,handle);if(!page||page->subpage)return PTS_tserrInvalidQuery;
    page->details.fskupd=fskupdNoChange;
    if(page->section){page->section->update=fskupdNoChange;page->section->description.fsupdinf.fskupd=fskupdNoChange;page->details.u.complex.fsupdinfPageBody.fskupd=fskupdNoChange;for(const auto& track:page->section->columns)clear_track(track);}
    else {page->details.u.simple.trackdescr.fsupdinf.fskupd=fskupdNoChange;clear_track(page->track);}return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsClearUpdateInfoInSubtrack(void* context,void* handle) {
    auto track=lookup_owned(tracks,context,handle);if(!track)return PTS_tserrInvalidQuery;clear_track(track);return 0;
}

namespace {
std::shared_ptr<SectionData> find_section(void* context,void* handle) {
    std::lock_guard<std::mutex> lock(gate);auto it=sections.find(handle);
    if(contexts.find(context)==contexts.end()||it==sections.end())return {};
    auto section=it->second.lock();if(!section||section->context.get()!=context)return {};return section;
}
}
extern "C" __declspec(dllexport) int WINAPI FsQueryPageSectionList(void* context,void* handle,int capacity,FSSECTIONDESCRIPTION* output,int* count) {
    if(!count)return PTS_tserrNullOutputParameter;*count=0;auto page=lookup_owned(pages,context,handle);if(!page||page->subpage||!page->section)return PTS_tserrInvalidQuery;
    if(capacity<1||!output)return PTS_tserrInvalidParameter;*output=page->section->description;*count=1;return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQuerySectionDetails(void* context,void* handle,FSSECTIONDETAILS* output) {
    if(!output)return PTS_tserrNullOutputParameter;*output={};auto section=find_section(context,handle);if(!section)return PTS_tserrInvalidQuery;
    output->fFootnotesAsPagenotes=1;auto& body=output->u.withpagenotes;body.fswdir=0;body.fsrcSectionBody=section->description.fsrc;body.fsbboxSectionBody=section->description.fsbbox;body.cBasicColumns=int(section->columns.size());return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQuerySectionBasicColumnList(void* context,void* handle,int capacity,FSTRACKDESCRIPTION* output,int* count) {
    if(!count)return PTS_tserrNullOutputParameter;*count=0;auto section=find_section(context,handle);if(!section)return PTS_tserrInvalidQuery;
    if(capacity<int(section->columns.size())||(!output&&!section->columns.empty()))return PTS_tserrInvalidParameter;
    for(const auto& track:section->columns){auto& desc=output[(*count)++];desc={};desc.fsupdinf.fskupd=track->update;desc.nms=track->segment;desc.fsrc=track->rect;desc.fsbbox=track->bbox;desc.pfstrack=track.get();}return 0;
}

namespace {
int build_subpage(const std::shared_ptr<Context>& context,void* segment,uint32_t direction,const FSRECT& rect,
    int width,int height,int columns,const FSCOLUMNINFO* info,bool bottomless,bool empty_ok,int suppress,bool previous,
    FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA suppress_break,const std::shared_ptr<Continuation>& input,
    std::shared_ptr<PageData>& result,void** margin_output,int& used,FSBBOX& bbox) {
    if(!segment||direction!=PTS_fswdirES||width<=0||height<0||columns<1||!info||rect.u<0||rect.v<0||rect.du<=0||rect.dv<0||
       int64_t(rect.u)+rect.du>width||(!bottomless&&int64_t(rect.v)+rect.dv>height))return PTS_tserrInvalidGeometry;
    if(bottomless&&columns!=1)return PTS_tserrNotImplemented;
    int64_t u=rect.u;
    for(int index=0;index<columns;++index){if(info[index].durBefore<0||info[index].durWidth<=0)return PTS_tserrInvalidGeometry;u+=int64_t(info[index].durBefore)+info[index].durWidth;if(u>int64_t(rect.u)+rect.du)return PTS_tserrInvalidGeometry;}
    auto page=std::make_shared<PageData>(context);page->subpage=true;page->bottomless=bottomless;
    if(columns>1){page->section=std::make_shared<SectionData>(context);page->section->description.nms=segment;page->section->description.fsrc=rect;}
    auto position=input;bool finished=false,stop_page=false;u=rect.u;
    for(int index=0;index<columns;++index) {
        u+=info[index].durBefore;FSRECT column{int(u),rect.v,info[index].durWidth,bottomless?INT32_MAX-rect.v:rect.dv};u+=column.du;
        std::shared_ptr<TrackData> track;MarginOwner margin{context};
        if(finished||stop_page){track=std::make_shared<TrackData>(context);track->segment=segment;track->rect=column;}
        else {
            int status=format_track(context,segment,direction,column,suppress,nullptr,track,&margin.value,bottomless,position,empty_ok,previous&&index==0,
                index==0?suppress_break:fsksuppresshardbreakbeforefirstparaColumn);if(status)return status;
            position=track->continuation;finished=!position;page->stop=track->stop;
            stop_page=track->stop==fmtrBrokenPageBreakBeforePara||track->stop==fmtrNoProgressPageBreakBeforePara;
        }
        page->forced_progress=page->forced_progress||track->forced_progress;
        int status=merge_bbox(bbox,track->bbox);if(status)return status;used=std::max(used,track->used);
        page->track=track;if(page->section)page->section->columns.push_back(track);
        if(index==columns-1){*margin_output=margin.value;margin.value=nullptr;}
    }
    page->continuation=position;auto& details=page->subpage_details;
    details.fSimple=columns==1;
    if(details.fSimple){auto& simple=details.u.simple;simple.fswdir=direction;auto& desc=simple.trackdescr;desc.fsupdinf.fskupd=fskupdNew;desc.nms=segment;desc.fsrc=page->track->rect;desc.fsbbox=page->track->bbox;desc.pfstrack=page->track.get();}
    else {auto& complex=details.u.complex;complex.nms=segment;complex.fswdir=direction;complex.fsrc=rect;complex.fsbbox=bbox;complex.cBasicColumns=columns;}
    result=page;return 0;
}
void clear_subpage(const std::shared_ptr<PageData>& page) {
    if(page->section)for(const auto& track:page->section->columns)clear_track(track);
    else {clear_track(page->track);page->subpage_details.u.simple.trackdescr.fsupdinf.fskupd=fskupdNoChange;}
}
}
extern "C" __declspec(dllexport) int WINAPI FsCreateSubpageFinite(void* context_handle,void* break_input,int previous,void* segment,
    void* footnote,int empty_ok,int suppress,uint32_t direction,int width,int height,const FSRECT* rect,int columns,const FSCOLUMNINFO* info,
    int balance,int segment_areas,void* const* segments,const int* segment_spans,int height_areas,const int* heights,const int* height_spans,
    int overhang,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA suppress_break,FSFMTR* formatted,void** output,void** break_output,
    int* used,FSBBOX* bbox,void** margin_output,int* top_space) {
    if(!formatted||!output||!break_output||!used||!bbox||!margin_output||!top_space)return PTS_tserrNullOutputParameter;
    *formatted={};*output=*break_output=*margin_output=nullptr;*used=*top_space=0;*bbox={};
    auto context=find_context(context_handle);if(!context)return PTS_tserrInvalidPtsContext;
    if(!rect)return PTS_tserrInvalidParameter;
    if(footnote||balance||segment_areas||height_areas||overhang)return PTS_tserrNotImplemented;
    std::shared_ptr<BreakData> input;
    if(break_input){input=lookup_owned(records,context_handle,break_input);if(!input||input->page||!input->subpage)return PTS_tserrInvalidBreakRecord;}
    try {
        std::shared_ptr<PageData> page;MarginOwner margin{context};
        int status=build_subpage(context,segment,direction,*rect,width,height,columns,info,false,empty_ok,suppress,previous,suppress_break,input?input->position:nullptr,page,&margin.value,*used,*bbox);if(status){*used=0;*bbox={};return status;}
        auto outgoing=page->continuation?make_record(context,page->continuation,false,nullptr,true):nullptr;
        try {if(callback_pages)callback_pages->push_back(page);register_page(page);}catch(...){if(outgoing)destroy_record(context_handle,outgoing.get(),false,true);throw;}
        formatted->kstop=page->stop;formatted->fForcedProgress=page->forced_progress;*output=page.get();*break_output=outgoing?outgoing.get():nullptr;
        *margin_output=margin.value;margin.value=nullptr;return 0;
    }catch(const std::bad_alloc&){*used=0;*bbox={};return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsCreateSubpageBottomless(void* context_handle,void* segment,int suppress,uint32_t direction,int width,
    int u,int dur,int v,int columns,const FSCOLUMNINFO* info,int segment_areas,void* const* segments,const int* segment_spans,
    int height_areas,const int* heights,const int* height_spans,int interruptible,FSFMTRBL* formatted,void** output,int* used,FSBBOX* bbox,
    void** margin_output,int* top_space,int* uninterruptible) {
    if(!formatted||!output||!used||!bbox||!margin_output||!top_space||!uninterruptible)return PTS_tserrNullOutputParameter;
    *formatted=fmtrblGoalReached;*output=*margin_output=nullptr;*used=*top_space=*uninterruptible=0;*bbox={};
    auto context=find_context(context_handle);if(!context)return PTS_tserrInvalidPtsContext;
    if(segment_areas||height_areas)return PTS_tserrNotImplemented;if(v<0)return PTS_tserrInvalidGeometry;
    try {
        FSRECT rect{u,v,dur,INT32_MAX-v};std::shared_ptr<PageData> page;MarginOwner margin{context};
        int status=build_subpage(context,segment,direction,rect,width,0,columns,info,true,false,suppress,true,fsksuppresshardbreakbeforefirstparaPageAndColumn,{},page,&margin.value,*used,*bbox);if(status){*used=0;*bbox={};return status;}
        if(callback_pages)callback_pages->push_back(page);register_page(page);
        *output=page.get();*margin_output=margin.value;margin.value=nullptr;*uninterruptible=1;return 0;
    }catch(const std::bad_alloc&){*used=0;*bbox={};return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsDestroySubpage(void* context,void* handle) {
    auto page=lookup_owned(pages,context,handle);if(!page||!page->subpage)return PTS_tserrInvalidQuery;
    unregister_page(page);return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsUpdateBottomlessSubpage(void* context_handle,void* handle,void* segment,int suppress,uint32_t direction,int width,
    int u,int dur,int v,int columns,const FSCOLUMNINFO* info,int segment_areas,void* const* segments,const int* segment_spans,
    int height_areas,const int* heights,const int* height_spans,int interruptible,FSFMTRBL* formatted,int* used,FSBBOX* bbox,
    void** margin_output,int* top_space,int* uninterruptible) {
    if(!formatted||!used||!bbox||!margin_output||!top_space||!uninterruptible)return PTS_tserrNullOutputParameter;
    *formatted=fmtrblGoalReached;*margin_output=nullptr;*used=*top_space=*uninterruptible=0;*bbox={};
    auto page=lookup_owned(pages,context_handle,handle);
    if(!page||!page->subpage||!page->bottomless||page->section||page->track->segment!=segment)return PTS_tserrInvalidQuery;
    if(segment_areas||height_areas)return PTS_tserrNotImplemented;if(v<0)return PTS_tserrInvalidGeometry;
    try {
        FSRECT rect{u,v,dur,INT32_MAX-v};std::shared_ptr<PageData> replacement;MarginOwner margin{page->context};
        int next_used=0;FSBBOX next_box{};
        int status=build_subpage(page->context,segment,direction,rect,width,0,columns,info,true,false,suppress,true,fsksuppresshardbreakbeforefirstparaPageAndColumn,{},replacement,&margin.value,next_used,next_box);if(status)return status;
        {
            std::lock_guard<std::mutex> lock(gate);
            // Allocate the new registry entry before changing any old state.
            // Retain subpage identity; superseded clients are released below,
            // outside the registry lock, through their original callbacks.
            tracks.emplace(replacement->track.get(),replacement->track);
            tracks.erase(page->track.get());
            page->track.swap(replacement->track);
            std::swap(page->subpage_details,replacement->subpage_details);
            page->continuation.swap(replacement->continuation);
            std::swap(page->stop,replacement->stop);
            std::swap(page->forced_progress,replacement->forced_progress);
        }
        *used=next_used;*bbox=next_box;*margin_output=margin.value;margin.value=nullptr;*uninterruptible=1;return 0;
    }catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsDestroySubpageBreakRecord(void* context,void* handle){return destroy_record(context,handle,false,true);}
extern "C" __declspec(dllexport) int WINAPI FsDuplicateSubpageBreakRecord(void* context,void* handle,void** output) {
    if(!output)return PTS_tserrNullOutputParameter;*output=nullptr;
    auto original=lookup_owned(records,context,handle);if(!original||original->page||!original->subpage)return PTS_tserrInvalidBreakRecord;
    try {auto record=make_record(original->context,original->position,false,nullptr,true);*output=record.get();return 0;}catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsClearUpdateInfoInSubpage(void* context,void* handle) {
    auto page=lookup_owned(pages,context,handle);if(!page||!page->subpage)return PTS_tserrInvalidQuery;clear_subpage(page);return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQuerySubpageDetails(void* context,void* handle,FSSUBPAGEDETAILS* output) {
    if(!output)return PTS_tserrNullOutputParameter;*output={};auto page=lookup_owned(pages,context,handle);if(!page||!page->subpage)return PTS_tserrInvalidQuery;
    *output=page->subpage_details;return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQuerySubpageBasicColumnList(void* context,void* handle,int capacity,FSTRACKDESCRIPTION* output,int* count) {
    if(!count)return PTS_tserrNullOutputParameter;*count=0;auto page=lookup_owned(pages,context,handle);if(!page||!page->subpage)return PTS_tserrInvalidQuery;
    size_t size=page->section?page->section->columns.size():1;if(capacity<0||size_t(capacity)<size||!output)return PTS_tserrInvalidParameter;
    auto put=[&](const std::shared_ptr<TrackData>& track){auto& desc=output[(*count)++];desc={};desc.fsupdinf.fskupd=track->update;desc.nms=track->segment;desc.fsrc=track->rect;desc.fsbbox=track->bbox;desc.pfstrack=track.get();};
    if(page->section)for(const auto& track:page->section->columns)put(track);else put(page->track);return 0;
}
