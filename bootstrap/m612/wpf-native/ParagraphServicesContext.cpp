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
constexpr uint32_t table_magic=0x54424c31;
struct TableHandler {uint32_t magic=table_magic;FSTABLEOBJINIT init{};};
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
extern "C" __declspec(dllexport) int WINAPI GetTableObjHandlerInfo(const FSTABLEOBJINIT* init,void* output) {
    if(!output)return PTS_tserrNullOutputParameter;if(!init)return PTS_tserrInvalidParameter;
    TableHandler handler{};handler.init=*init;std::memcpy(output,&handler,sizeof(handler));return 0;
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
struct FloaterBreakData {
    std::shared_ptr<Context> context;
    FSFLOATERCBK callbacks{};
    void* handle=nullptr;
    explicit FloaterBreakData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~FloaterBreakData(){if(handle)reinterpret_cast<PtsDestroyFloaterContentBreakRecord>(callbacks.pfnDestroyFloaterContentBreakRecord)(context->info.pfsclient,handle);--context->resources;}
};
struct PendingFloater { int anchor=0; void* name=nullptr; std::shared_ptr<FloaterBreakData> record; };
struct CellBreakData {
    std::shared_ptr<Context> context;FSTABLECBKCELL callbacks{};void* handle=nullptr;
    explicit CellBreakData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~CellBreakData(){if(handle)reinterpret_cast<PtsDestroyCellBreakRecord>(callbacks.pfnDestroyCellBreakRecord)(context->info.pfsclient,handle);--context->resources;}
};
struct PendingTableCell {void* name=nullptr;std::shared_ptr<CellBreakData> record;bool done=false;};
struct Continuation {
    std::shared_ptr<Context> context;
    void* segment=nullptr;
    void* paragraph=nullptr;
    int dcp=0;
    std::shared_ptr<LineBreakData> line_break;
    std::shared_ptr<BreakData> child;
    int child_object=0;
    void* table_row=nullptr;
    std::vector<PendingTableCell> table_cells;
    std::vector<PendingTableCell> span_cells;
    bool text_ended=false;
    std::vector<PendingFloater> floaters;
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

struct FigureData { FSFIGUREPROPS props{}; FSFIGUREDETAILS details{}; };
struct ParaData;
struct AttachedData { int anchor=0; FSFLOATERPROPS props{}; std::shared_ptr<ParaData> para; };
struct TextData {
    std::shared_ptr<Context> context;
    explicit TextData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    uint32_t direction=0;
    std::vector<FSLINEDESCRIPTIONSINGLE> lines;
    // Output break records include the last record, which is not necessarily
    // referenced by another line. Retain each record exactly once.
    std::vector<std::shared_ptr<LineBreakData>> breaks;
    std::shared_ptr<LineBreakData> incoming;
    std::vector<AttachedData> attached;
    std::vector<PendingFloater> pending_floaters;
    ~TextData() {
        erase_text(this);
        auto& cb=context->info.fscbk.cbktxt;void* client=context->info.pfsclient;
        for(auto& line:lines)if(line.pfslineclient)reinterpret_cast<PtsDestroyLine>(cb.pfnDestroyLine)(client,line.pfslineclient);
        attached.clear();pending_floaters.clear();breaks.clear();incoming.reset();
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
struct TableData;
struct TableRowData;
void erase_table(void*);
void erase_table_row(void*);
struct TableCellData {
    std::shared_ptr<Context> context;
    FSTABLECBKCELL callbacks{};
    void* name=nullptr;void* handle=nullptr;void* record=nullptr;int height=0;
    FSKUPDATE update=fskupdNew;
    std::weak_ptr<TableCellData> transferred_from;
    explicit TableCellData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~TableCellData(){if(record)reinterpret_cast<PtsDestroyCellBreakRecord>(callbacks.pfnDestroyCellBreakRecord)(context->info.pfsclient,record);if(handle)reinterpret_cast<PtsDestroyCell>(callbacks.pfnDestroyCell)(handle);--context->resources;}
};
struct TableRowData {
    std::shared_ptr<Context> context;
    FSTABLEROWDESCRIPTION description{};FSTABLEROWDETAILS details{};
    std::vector<std::shared_ptr<TableCellData>> cells;
    std::vector<FSTABLEKCELLMERGE> merges;
    explicit TableRowData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~TableRowData(){erase_table_row(this);cells.clear();--context->resources;}
};
struct TableData {
    std::shared_ptr<Context> context;FSTABLEOBJINIT callbacks{};
    FSTABLEOBJDETAILS details{};FSTABLEDETAILS proper{};
    std::vector<std::shared_ptr<TableRowData>> rows;
    explicit TableData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~TableData(){erase_table(this);rows.clear();--context->resources;}
};
struct PageData;
struct ParaData {
    std::shared_ptr<Context> context;
    FSPARADESCRIPTION desc{};
    bool forced_progress=false;
    bool keep_together=false,keep_with_next=false;
    int min_lines_before_break=1;
    uint32_t direction=0;
    std::weak_ptr<ParaData> transferred_from;
    std::shared_ptr<TextData> text;
    std::shared_ptr<FloaterData> floater;
    std::shared_ptr<FigureData> figure;
    std::vector<std::shared_ptr<PageData>> figure_pages;
    std::shared_ptr<TableData> table;
    ~ParaData();
};
struct TrackData {
    std::shared_ptr<Context> context;
    explicit TrackData(std::shared_ptr<Context> value):context(std::move(value)){++context->resources;}
    ~TrackData(){paras.clear();--context->resources;}
    void* segment=nullptr;
    uint32_t direction=0;
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
    uint32_t direction=0;
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
    bool inherit_updates=false;
    FSSUBPAGEDETAILS subpage_details{};
    std::shared_ptr<Continuation> continuation;
    FSFMTRKSTOP stop=fmtrGoalReached;
    bool forced_progress=false;
};
std::unordered_map<void*,std::shared_ptr<TrackData>> tracks;
std::unordered_map<void*,std::weak_ptr<TextData>> texts;
std::unordered_map<void*,std::weak_ptr<FloaterData>> floaters;
std::unordered_map<void*,std::weak_ptr<ParaData>> figures;
std::unordered_map<void*,std::shared_ptr<PageData>> pages;
std::unordered_map<void*,std::shared_ptr<BreakData>> records;
std::unordered_map<void*,std::weak_ptr<SectionData>> sections;
std::unordered_map<void*,std::weak_ptr<TableData>> tables;
std::unordered_map<void*,std::weak_ptr<TableRowData>> table_rows;
void erase_table(void* handle){std::lock_guard<std::mutex> lock(gate);tables.erase(handle);}
void erase_table_row(void* handle){std::lock_guard<std::mutex> lock(gate);table_rows.erase(handle);}
void erase_section(void* handle){std::lock_guard<std::mutex> lock(gate);sections.erase(handle);}
void erase_text(void* handle){std::lock_guard<std::mutex> lock(gate);texts.erase(handle);}
void erase_floater(void* handle){std::lock_guard<std::mutex> lock(gate);floaters.erase(handle);}
ParaData::~ParaData() {
    // Arranged original subpage clients own their subpage handle themselves.
    // Let them dispose it first; an unarranged client leaves native ownership
    // here. Container clients retain the original object-before-client order.
    if((desc.idobj==1||figure)&&desc.pfsparaclient){reinterpret_cast<PtsDestroyParaclient>(context->info.fscbk.cbkgen.pfnDestroyParaclient)(context->info.pfsclient,desc.pfsparaclient);desc.pfsparaclient=nullptr;}
    bool destroy_subpage=false;
    if(desc.idobj==1&&desc.pfspara){std::lock_guard<std::mutex> lock(gate);auto found=pages.find(desc.pfspara);destroy_subpage=found!=pages.end()&&found->second->subpage&&found->second->context==context;}
    if(desc.pfspara&&(desc.idobj==0||destroy_subpage))reinterpret_cast<PtsObjDestroyPara>(context->installed->methods[desc.idobj].pfnDestroyPara)(desc.pfspara);
    if(figure){std::lock_guard<std::mutex> lock(gate);figures.erase(figure.get());}
    figure.reset();figure_pages.clear();text.reset();floater.reset();table.reset();
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
    bool claimed_pages=false;
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
        for(auto& page:created_pages)if(!claimed_pages&&page.get()!=claimed){unregister_page(page);page.reset();}
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

struct FigureGeometry {
    FSRECT page,margin;
    FigureGeometry* previous;
    static thread_local FigureGeometry* current;
    FigureGeometry(const FSRECT& p,const FSRECT& m):page(p),margin(m),previous(current){current=this;}
    ~FigureGeometry(){current=previous;}
};
thread_local FigureGeometry* FigureGeometry::current=nullptr;
FSRECT& attachment_rect(const std::shared_ptr<ParaData>& para) {
    return para->figure?para->figure->details.fsrcFlowAround:para->floater->details.fsrcFloater;
}
int format_figure(const std::shared_ptr<ParaData>& para,uint32_t direction,const FSRECT& track,bool bottomless,bool force,bool& deferred) {
    if(bottomless)return PTS_tserrNotImplemented;
    auto& cb=para->context->info.fscbk.cbkfig;
    if(!cb.pfnGetFigureProperties||!cb.pfnCalcFigurePosition)return PTS_tserrInvalidParameter;
    auto data=std::make_shared<FigureData>();para->figure=data;
    int width=0,height=0,polygons=0,vertices=0,left=0,right=0,top=0,bottom=0;CallbackTracks pending;
    int status=reinterpret_cast<PtsGetFigureProperties>(cb.pfnGetFigureProperties)(para->context->info.pfsclient,para->desc.pfsparaclient,para->desc.nmp,1,direction,0,&width,&height,&data->props,&polygons,&vertices,&left,&right,&top,&bottom);
    if(status)return status;
    // The original FigureParaClient owns successful subpage output, including
    // autosizing reformat. It disposes that handle before its native references.
    pending.claimed_pages=true;
    for(const auto& page:pending.created_pages)if(lookup_owned(pages,para->context.get(),page.get()))para->figure_pages.push_back(page);
    if(width<0||height<0||left<0||right<0||top<0||bottom<0)return PTS_tserrInvalidClientOutput;
    if(polygons||vertices||data->props.fNonTextPlane||data->props.fAllowOverlap)return PTS_tserrNotImplemented;
    FSRECT page=FigureGeometry::current?FigureGeometry::current->page:track;
    FSRECT margin=FigureGeometry::current?FigureGeometry::current->margin:track;
    auto origin=[&](FSKREF reference,FSKALIGNFIG alignment,bool horizontal,int size,int& out)->bool {
        const FSRECT* base=reference==fskrefPage?&page:reference==fskrefMargin?&margin:reference==fskrefParagraph||reference==fskrefChar?&track:nullptr;
        if(!base||alignment<fskalfMin||alignment>fskalfMax)return false;
        int64_t value=horizontal?base->u:base->v;int extent=horizontal?base->du:base->dv;
        if(alignment==fskalfCenter)value+=(int64_t(extent)-size)/2;
        if(alignment==fskalfMax)value+=int64_t(extent)-size;
        if(value<INT32_MIN||value>INT32_MAX)return false;out=int(value);return true;
    };
    FSRECT preliminary{0,0,width,height};
    if(!origin(data->props.fskrefU,data->props.fskalfU,true,width,preliminary.u)||!origin(data->props.fskrefV,data->props.fskalfV,false,height,preliminary.v))return PTS_tserrNotImplemented;
    int push=0;FSRECT overlap{},search{},track_rect=track;auto& details=data->details;details.fsptPosPreliminary={preliminary.u,preliminary.v};
    status=reinterpret_cast<PtsCalcFigurePosition>(cb.pfnCalcFigurePosition)(para->context->info.pfsclient,para->desc.pfsparaclient,para->desc.nmp,direction,&page,&margin,&track_rect,&preliminary,force||!data->props.fDelayable,1,&push,&details.fsrcFlowAround,&overlap,&details.fsbbox,&search);
    if(status)return status;
    auto& rect=details.fsrcFlowAround;
    if(rect.du<0||rect.dv<0||int64_t(rect.u)+rect.du>INT32_MAX||int64_t(rect.v)+rect.dv>INT32_MAX)return PTS_tserrInvalidClientOutput;
    if(push&&data->props.fDelayable&&!force){deferred=true;return 0;}
    para->desc.pfspara=data.get();para->desc.fsbbox=details.fsbbox;para->desc.dvrUsed=rect.dv;
    {std::lock_guard<std::mutex> lock(gate);figures[data.get()]=para;}
    return 0;
}
int format_block_ui(const std::shared_ptr<ParaData>& para,uint32_t direction,const FSRECT& rect,
    void* input_margin,bool bottomless,bool empty_ok,int suppress,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA suppress_break,
    std::shared_ptr<Continuation>& output,void** margin_output,bool floating=false,FSFLOATERPROPS* floater_props=nullptr,
    void* floater_input=nullptr,std::shared_ptr<FloaterBreakData>* floater_output=nullptr,bool previous_page=true);
int format_attached_objects(const std::shared_ptr<ParaData>& para,int dcp,uint32_t direction,const FSRECT& rect,bool bottomless,bool force,
    const std::vector<void*>& names,const std::vector<int>& kinds,const std::vector<int>& anchors,int& maximum_anchor,bool& deferred,const PendingFloater* resume=nullptr,bool previous_page=true) {
    auto context=para->context;void* client=context->info.pfsclient;
    int count=int(names.size()),status=0;deferred=false;
    for(int index=0;index<count;++index) {
        maximum_anchor=std::max(maximum_anchor,anchors[index]);
        if((kinds[index]!=2&&kinds[index]!=-2)||!names[index]||anchors[index]<dcp)return PTS_tserrNotImplemented;
        auto& attached=para->text->attached;
        if(std::any_of(attached.begin(),attached.end(),[&](const AttachedData& value){return value.para->desc.nmp==names[index];}))continue;
        auto child=std::make_shared<ParaData>();child->context=context;child->direction=direction;child->desc.nmp=names[index];child->desc.idobj=kinds[index];child->desc.fsupdinf.fskupd=fskupdNew;
        status=reinterpret_cast<PtsCreateParaclient>(context->info.fscbk.cbkgen.pfnCreateParaclient)(client,names[index],&child->desc.pfsparaclient);if(status)return status;
        if(!child->desc.pfsparaclient)return PTS_tserrInvalidClientOutput;
        std::shared_ptr<Continuation> next;std::shared_ptr<FloaterBreakData> tail;MarginOwner margin{context};FSFLOATERPROPS props{};
        if(kinds[index]==-2){status=format_figure(child,direction,rect,bottomless,force,deferred);if(status||deferred)return status;props.fskwr=child->figure->props.fskwrap;}
        else {status=format_block_ui(child,direction,rect,nullptr,bottomless,!force,1,fsksuppresshardbreakbeforefirstparaPageAndColumn,next,&margin.value,true,&props,resume?resume->record->handle:nullptr,&tail,previous_page);if(status)return status;}
        if(next){if(force)return PTS_tserrNotImplemented;deferred=true;return 0;}
        auto& candidate=attachment_rect(child);
        int64_t placed=candidate.v;
        // Keep each object's requested horizontal edge. If its rectangle
        // collides with an earlier object, move it below that rectangle.
        // Opposite edges can share a row when their rectangles do not overlap.
        for(;;){int64_t next_top=placed;
            for(const auto& previous:attached){const auto& obstacle=attachment_rect(previous.para);
                bool horizontal=int64_t(candidate.u)<int64_t(obstacle.u)+obstacle.du&&int64_t(obstacle.u)<int64_t(candidate.u)+candidate.du;
                if(horizontal&&placed<int64_t(obstacle.v)+obstacle.dv&&int64_t(obstacle.v)<placed+candidate.dv)next_top=std::max(next_top,int64_t(obstacle.v)+obstacle.dv);
            }
            if(next_top==placed)break;placed=next_top;
        }
        if(placed+candidate.dv>INT32_MAX)return PTS_tserrInvalidGeometry;
        int64_t placement_bottom=int64_t(rect.v)+rect.dv;
        if(child->figure&&FigureGeometry::current){auto reference=child->figure->props.fskrefV;if(reference==fskrefPage)placement_bottom=int64_t(FigureGeometry::current->page.v)+FigureGeometry::current->page.dv;else if(reference==fskrefMargin)placement_bottom=int64_t(FigureGeometry::current->margin.v)+FigureGeometry::current->margin.dv;}
        if(!bottomless&&placed+candidate.dv>placement_bottom){if(force)return PTS_tserrNotImplemented;deferred=true;return 0;}
        int64_t shift=placed-candidate.v;
        auto& box=child->desc.fsbbox;
        if(box.fDefined&&(int64_t(box.fsrc.v)+shift<INT32_MIN||int64_t(box.fsrc.v)+shift+box.fsrc.dv>INT32_MAX))return PTS_tserrInvalidGeometry;
        candidate.v=int(placed);if(box.fDefined)box.fsrc.v=int(int64_t(box.fsrc.v)+shift);if(child->figure)child->figure->details.fsbbox=box;
        attached.push_back({anchors[index],props,child});
        if(tail)para->text->pending_floaters.push_back({anchors[index],names[index],tail});
    }
    return 0;
}
int attached_before_line(const std::shared_ptr<ParaData>& para,int dcp,uint32_t direction,const FSRECT& rect,bool bottomless,bool force,int& maximum_anchor,bool& deferred) {
    auto context=para->context;auto& cb=context->info.fscbk.cbktxt;void* client=context->info.pfsclient;
    maximum_anchor=-1;deferred=false;
    if(!cb.pfnGetNumberAttachedObjectsBeforeTextLine)return 0;
    int count=0,status=reinterpret_cast<PtsGetNumberAttachedObjectsBeforeTextLine>(cb.pfnGetNumberAttachedObjectsBeforeTextLine)(client,para->desc.nmp,dcp,&count);if(status)return status;
    if(count<0)return PTS_tserrInvalidClientOutput;if(!count)return 0;
    if(!cb.pfnGetAttachedObjectsBeforeTextLine)return PTS_tserrInvalidParameter;
    std::vector<void*> names(count);std::vector<int> kinds(count),anchors(count);int actual=0,ended=0;
    status=reinterpret_cast<PtsGetAttachedObjectsBeforeTextLine>(cb.pfnGetAttachedObjectsBeforeTextLine)(client,para->desc.nmp,dcp,count,names.data(),kinds.data(),anchors.data(),&actual,&ended);if(status)return status;
    if(actual!=count||ended)return PTS_tserrInvalidClientOutput;
    return format_attached_objects(para,dcp,direction,rect,bottomless,force,names,kinds,anchors,maximum_anchor,deferred);
}
int attached_in_line(const std::shared_ptr<ParaData>& para,const FSLINEDESCRIPTIONSINGLE& line,uint32_t direction,const FSRECT& rect,
    bool bottomless,bool force,int& maximum_anchor,bool& deferred) {
    auto context=para->context;auto& cb=context->info.fscbk.cbktxt;void* client=context->info.pfsclient;
    if(!cb.pfnGetNumberAttachedObjectsInTextLine)return 0;
    int count=0;
    int status=reinterpret_cast<PtsGetNumberAttachedObjectsInTextLine>(cb.pfnGetNumberAttachedObjectsInTextLine)(client,line.pfslineclient,para->desc.nmp,line.dcpFirst,line.dcpLim,maximum_anchor>=0,maximum_anchor,&count);if(status)return status;
    if(count<0)return PTS_tserrInvalidClientOutput;if(!count)return 0;
    if(!cb.pfnGetAttachedObjectsInTextLine)return PTS_tserrInvalidParameter;
    std::vector<void*> names(count);std::vector<int> kinds(count),anchors(count);int actual=0;
    status=reinterpret_cast<PtsGetAttachedObjectsInTextLine>(cb.pfnGetAttachedObjectsInTextLine)(client,line.pfslineclient,para->desc.nmp,line.dcpFirst,line.dcpLim,maximum_anchor>=0,maximum_anchor,count,names.data(),kinds.data(),anchors.data(),&actual);if(status)return status;
    if(actual!=count)return PTS_tserrInvalidClientOutput;
    for(int anchor:anchors)if(anchor<line.dcpFirst||anchor>=line.dcpLim)return PTS_tserrInvalidClientOutput;
    return format_attached_objects(para,line.dcpFirst,direction,rect,bottomless,force,names,kinds,anchors,maximum_anchor,deferred);
}
int postpone_unconsumed_objects(TextData& text,const FSLINEDESCRIPTIONSINGLE& line,const FSRECT& rect,bool bottomless,bool& deferred) {
    // Narrowing can move a discovered anchor out of its provisional line.
    // Keep the chosen text break and move its rectangle forward monotonically
    // so retrying cannot oscillate between wide and narrow line layouts.
    for(auto& object:text.attached) {
        if(object.anchor<line.dcpLim)continue;
        if(object.para->figure&&object.para->figure->props.fskrefV!=fskrefParagraph&&object.para->figure->props.fskrefV!=fskrefChar)continue;
        auto& candidate=attachment_rect(object.para);
        int64_t placed=std::max(int64_t(candidate.v),int64_t(line.vrStart)+line.dvrAscent+line.dvrDescent);
        for(;;){int64_t next=placed;
            for(const auto& earlier:text.attached){if(&earlier==&object)continue;const auto& obstacle=attachment_rect(earlier.para);
                if(earlier.anchor<object.anchor&&int64_t(candidate.u)<int64_t(obstacle.u)+obstacle.du&&int64_t(obstacle.u)<int64_t(candidate.u)+candidate.du&&placed<int64_t(obstacle.v)+obstacle.dv&&int64_t(obstacle.v)<placed+candidate.dv)next=std::max(next,int64_t(obstacle.v)+obstacle.dv);
            }
            if(next==placed)break;placed=next;
        }
        if(placed+candidate.dv>INT32_MAX)return PTS_tserrInvalidGeometry;
        if(!bottomless&&placed+candidate.dv>int64_t(rect.v)+rect.dv){deferred=true;return 0;}
        auto& box=object.para->desc.fsbbox;int64_t shift=placed-candidate.v;
        if(box.fDefined&&(int64_t(box.fsrc.v)+shift<INT32_MIN||int64_t(box.fsrc.v)+shift+box.fsrc.dv>INT32_MAX))return PTS_tserrInvalidGeometry;
        candidate.v=int(placed);if(box.fDefined)box.fsrc.v=int(int64_t(box.fsrc.v)+shift);if(object.para->figure)object.para->figure->details.fsbbox=box;
    }
    return 0;
}
int line_area(const TextData& text,const FSRECT& rect,int& used,int& u,int& width,int height_hint=0) {
    // Intersect every obstacle's permitted regions before choosing a contiguous
    // line interval. Choosing after each obstacle can discard a valid side.
    for(;;) {
        using Interval=std::pair<int64_t,int64_t>;
        std::vector<Interval> intervals{{rect.u,int64_t(rect.u)+rect.du}};
        int next=INT32_MAX;bool active=false;
        for(const auto& object:text.attached){const auto& obstacle=attachment_rect(object.para);int bottom=obstacle.v+obstacle.dv;
            if(int64_t(obstacle.v)>=int64_t(rect.v)+used+std::max(1,height_hint)||bottom<=rect.v+used)continue;
            active=true;next=std::min(next,bottom);auto wrap=object.props.fskwr;
            if(wrap==fskwrNone){intervals.clear();continue;}
            int64_t cut_left=wrap==fskwrRight?rect.u:obstacle.u;
            int64_t cut_right=wrap==fskwrLeft?int64_t(rect.u)+rect.du:int64_t(obstacle.u)+obstacle.du;
            std::vector<Interval> remaining;
            for(const auto& interval:intervals){
                if(cut_right<=interval.first||cut_left>=interval.second){remaining.push_back(interval);continue;}
                if(cut_left>interval.first)remaining.push_back({interval.first,std::min(cut_left,interval.second)});
                if(cut_right<interval.second)remaining.push_back({std::max(cut_right,interval.first),interval.second});
            }
            intervals=std::move(remaining);
        }
        auto chosen=std::max_element(intervals.begin(),intervals.end(),[](const Interval& a,const Interval& b){return a.second-a.first<b.second-b.first;});
        if(chosen!=intervals.end()&&chosen->second>chosen->first){u=int(chosen->first);width=int(chosen->second-chosen->first);return 0;}
        if(!active||next==INT32_MAX||next<rect.v)return PTS_tserrInvalidGeometry;
        used=next-rect.v;
    }
}

int format_text(const std::shared_ptr<ParaData>& para,uint32_t direction,const FSRECT& rect,
    const std::shared_ptr<Continuation>& input,std::shared_ptr<Continuation>& output,bool bottomless,bool force,bool keep_self=false,bool previous_page=true) {
    auto context=para->context;auto& cb=context->info.fscbk.cbktxt;void* client=context->info.pfsclient;
    if(!cb.pfnGetTextProperties||!cb.pfnFormatLine||!cb.pfnDestroyLine||!cb.pfnDestroyLineBreakRecord)return PTS_tserrInvalidParameter;
    FSTXTPROPS props{};int status=reinterpret_cast<PtsGetTextProperties>(cb.pfnGetTextProperties)(client,para->desc.nmp,0,&props);
    if(status)return status;
    if(keep_self)props.fKeepTogether=1;
    para->keep_together=props.fKeepTogether!=0;para->min_lines_before_break=std::max(1,props.cMinLinesBeforeBreak);
    if((props.fswdir!=PTS_fswdirES&&props.fswdir!=PTS_fswdirWS)||props.fDropCap||props.fVerticalGrid||props.fOptimizeParagraph)return PTS_tserrNotImplemented;
    if(props.fswdir!=direction){
        FSRECT page=FigureGeometry::current?FigureGeometry::current->page:rect,converted{};
        status=FsTransformRectangle(direction,&page,&rect,props.fswdir,&converted);if(status)return status;
        status=format_text(para,props.fswdir,converted,input,output,bottomless,force,keep_self,previous_page);if(status)return status;
        FSBBOX box{};status=FsTransformBbox(props.fswdir,&page,&para->desc.fsbbox,direction,&box);if(!status)para->desc.fsbbox=box;return status;
    }
    auto text=std::make_shared<TextData>(context);text->direction=props.fswdir;para->text=text;
    int dcp=input?input->dcp:props.dcpStartContent,used=0,fit=0,height_hint=0;bool overflow=false,ended=false,deferred=false;
    std::shared_ptr<LineBreakData> previous=input?input->line_break:nullptr;text->incoming=previous;
    if(input)for(const auto& pending:input->floaters) {
        int max_anchor=-1;
        status=format_attached_objects(para,pending.anchor,direction,rect,bottomless,force,{pending.name},{2},{pending.anchor},max_anchor,deferred,&pending,previous_page);if(status)return status;
        if(deferred){text->attached.clear();text->pending_floaters.clear();output=std::make_shared<Continuation>(*input);break;}
    }
    if(input&&input->text_ended)ended=true;
    while(!ended&&!deferred) {
        int max_anchor=-1;FSRECT object_area=rect;object_area.v+=used;object_area.dv=std::max(0,rect.dv-used);
        status=attached_before_line(para,dcp,direction,object_area,bottomless,force&&used==0,max_anchor,deferred);if(status)return status;
        // No host line has consumed this anchor. Resume with its incoming
        // line-break record, releasing speculative attached content below.
        if(deferred)break;
        int line_u=rect.u,line_width=rect.du;status=line_area(*text,rect,used,line_u,line_width,height_hint);if(status)return status;
        text->lines.emplace_back();text->breaks.push_back(std::make_shared<LineBreakData>(context));
        auto& line=text->lines.back();auto& br=text->breaks.back();
        line.dcpFirst=dcp;line.urStart=line_u;line.dur=line_width;line.vrStart=rect.v+used;
        line.pfsbreakreclineclient=previous?previous->handle:nullptr;line.fTreatedAsFirst=(dcp==props.dcpStartContent);
        int consumed=0,dependent=0,reformat=0;FSFLRES result{};
        status=reinterpret_cast<PtsFormatLine>(cb.pfnFormatLine)(client,para->desc.pfsparaclient,para->desc.nmp,0,dcp,line.pfsbreakreclineclient,direction,
            line_u,line_width,rect.u,rect.du,rect.u,0,0,0,line.fTreatedAsFirst,0,used==0,
            &line.pfslineclient,&consumed,&br->handle,&line.fForceBroken,&result,&line.dvrAscent,&line.dvrDescent,&line.urBBox,&line.durBBox,&dependent,&reformat);
        if(status)return status;
        if(!line.pfslineclient||consumed<=0||line.dvrAscent<0||line.dvrDescent<0||line.durBBox<0||int64_t(line.urBBox)+line.durBBox>INT32_MAX||int64_t(dcp)+consumed>INT32_MAX)return PTS_tserrInvalidClientOutput;
        int64_t height=int64_t(line.dvrAscent)+line.dvrDescent;
        // Structural-marker lines can consume source without visible height.
        if(height<0||height>INT32_MAX-int64_t(rect.v)-used)return PTS_tserrInvalidGeometry;
        line.dcpLim=dcp+consumed;
        // Existing obstacles may have advanced the actual line vertically.
        object_area.v=line.vrStart;object_area.dv=std::max(0,rect.dv-used);
        status=attached_in_line(para,line,direction,object_area,bottomless,force&&used==0,max_anchor,deferred);if(status)return status;
        if(!deferred)status=postpone_unconsumed_objects(*text,line,rect,bottomless,deferred);
        if(status)return status;
        if(deferred){
            reinterpret_cast<PtsDestroyLine>(cb.pfnDestroyLine)(client,line.pfslineclient);line.pfslineclient=nullptr;
            text->lines.pop_back();text->breaks.pop_back();break;
        }
        int next_used=used,next_u=line_u,next_width=line_width;
        status=line_area(*text,rect,next_used,next_u,next_width,int(height));if(status)return status;
        if(next_used>used||next_u>line_u||int64_t(next_u)+next_width<int64_t(line_u)+line_width){
            reinterpret_cast<PtsDestroyLine>(cb.pfnDestroyLine)(client,line.pfslineclient);line.pfslineclient=nullptr;
            text->lines.pop_back();text->breaks.pop_back();used=next_used;height_hint=std::max(height_hint,int(height));continue;
        }
        height_hint=0;
        line.dcpLim=dcp+consumed;dcp=line.dcpLim;previous=br;
        if(!overflow && height<=int64_t(rect.dv)-used)++fit;else overflow=true;
        used+=int(height);
        ended=result>=fsflrEndOfParagraph&&result<=fsflrEndOfParagraphClearBoth;
        if(!ended&&result!=fsflrOutOfSpace&&result!=fsflrSoftBreak)return PTS_tserrNotImplemented;
        if(ended || (!bottomless&&overflow&&int(text->lines.size())-fit>=std::max(1,props.cMinLinesAfterBreak)))break;
    }
    if(!bottomless && (fit<int(text->lines.size())||deferred)) {
        // Lookahead is enough to preserve widow/orphan policy, without retaining
        // speculative client lines in the formatted page or consuming their text.
        int fitting=fit;
        if(ended && int(text->lines.size())-fit<props.cMinLinesAfterBreak)
            fit=std::max(0,int(text->lines.size())-props.cMinLinesAfterBreak);
        if(fit<props.cMinLinesBeforeBreak)fit=0;
        if(!fit&&force)fit=fitting;
        if(!deferred&&!fit&&force&&!text->lines.empty()){fit=1;para->forced_progress=true;}
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
            if(!output)output=std::make_shared<Continuation>();output->context=context;output->paragraph=para->desc.nmp;
            output->dcp=fit?text->lines[fit-1].dcpLim:(input?input->dcp:props.dcpStartContent);
            output->line_break=fit?text->breaks[fit-1]:text->incoming;
        }
        for(size_t i=fit;i<text->lines.size();++i)if(text->lines[i].pfslineclient)reinterpret_cast<PtsDestroyLine>(cb.pfnDestroyLine)(client,text->lines[i].pfslineclient);
        text->lines.resize(fit);text->breaks.resize(fit);
    }
    used=0;
    for(const auto& line:text->lines) {
        int height=line.dvrAscent+line.dvrDescent;used=std::max(used,line.vrStart-rect.v+height);
        FSBBOX bbox{1,{line.urBBox,line.vrStart,line.durBBox,height}};status=merge_bbox(para->desc.fsbbox,bbox);if(status)return status;
    }
    int limit=text->lines.empty()?(input?input->dcp:props.dcpStartContent):text->lines.back().dcpLim;
    text->attached.erase(std::remove_if(text->attached.begin(),text->attached.end(),[&](const AttachedData& value){return value.anchor>=limit;}),text->attached.end());
    for(const auto& object:text->attached){status=merge_bbox(para->desc.fsbbox,object.para->desc.fsbbox);if(status)return status;const auto& obstacle=attachment_rect(object.para);used=std::max(used,object.para->figure?std::min(rect.dv,std::max(0,obstacle.v-rect.v+obstacle.dv)):obstacle.v-rect.v+obstacle.dv);}
    text->pending_floaters.erase(std::remove_if(text->pending_floaters.begin(),text->pending_floaters.end(),[&](const PendingFloater& value){return value.anchor>=limit;}),text->pending_floaters.end());
    if(!text->pending_floaters.empty()) {
        if(!output){output=std::make_shared<Continuation>();output->context=context;output->paragraph=para->desc.nmp;output->dcp=limit;output->line_break=text->lines.empty()?text->incoming:text->breaks.back();output->text_ended=ended;}
        output->floaters=text->pending_floaters;
    }
    para->desc.pfspara=text.get();para->desc.dvrUsed=para->forced_progress?std::min(used,rect.dv):used;
    {std::lock_guard<std::mutex> lock(gate);texts[text.get()]=text;}
    return 0;
}

int format_block_ui(const std::shared_ptr<ParaData>& para,uint32_t direction,const FSRECT& rect,
    void* input_margin,bool bottomless,bool empty_ok,int suppress,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA suppress_break,
    std::shared_ptr<Continuation>& output,void** margin_output,bool floating,FSFLOATERPROPS* floater_props,void* floater_input,std::shared_ptr<FloaterBreakData>* floater_output,bool previous_page) {
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
    if(bool(props.fFloat)!=floating||props.fskclear!=fskclearNone||(!floating&&props.fskwr!=fskwrNone))return PTS_tserrNotImplemented;
    if(floating&&props.fskfloatalignment==fskfloatalignCenter&&props.fskwr!=fskwrNone)return PTS_tserrNotImplemented;
    if(floater_props)*floater_props=props;
    int top=0;
    status=reinterpret_cast<PtsConvertMcsclient>(obj.pfnConvertMcsclient)(client,para->desc.pfsparaclient,para->desc.nmp,direction,input_margin,suppress,&top);if(status)return status;
    if(int64_t(rect.v)+top<INT32_MIN||int64_t(rect.v)+top>INT32_MAX||int64_t(rect.dv)-top>INT32_MAX)return PTS_tserrInvalidGeometry;
    auto data=std::make_shared<FloaterData>(context);data->callbacks=cb;para->floater=data;
    CallbackTracks pending;
    auto& details=data->details;details.fsnmFloater=para->desc.nmp;details.fskupdContent=fskupdNew;
    int width=0,height=0,polygons=0,vertices=0;FSBBOX box{};
    int available=std::max(0,rect.dv-top);FSFMTR finite{};
    if(bottomless) {
        FSFMTRBL result{};
        status=reinterpret_cast<PtsFormatFloaterContentBottomless>(cb.pfnFormatFloaterContentBottomless)(client,para->desc.pfsparaclient,para->desc.nmp,suppress,direction,1,rect.du,available,
            &result,&details.pfsFloaterContent,&width,&height,&box,&polygons,&vertices);
        if(!status&&result!=fmtrblGoalReached)status=PTS_tserrNotImplemented;
    } else {
        status=reinterpret_cast<PtsFormatFloaterContentFinite>(cb.pfnFormatFloaterContentFinite)(client,para->desc.pfsparaclient,floater_input,floater_input&&previous_page?1:0,para->desc.nmp,nullptr,empty_ok,suppress,direction,1,rect.du,available,suppress_break,
            &finite,&details.pfsFloaterContent,&data->break_record,&width,&height,&box,&polygons,&vertices);
    }
    pending.claimed=details.pfsFloaterContent;pending.claimed_record=data->break_record;
    if(status)return status;
    if(polygons||vertices)return PTS_tserrNotImplemented;
    // An attached floater is kept intact when the occupied page would split
    // its subpage. FloaterData releases both speculative content and record.
    if(!bottomless&&floating&&empty_ok&&!floater_input&&(data->break_record||finite.kstop==fmtrBrokenOutOfSpace)){
        output=std::make_shared<Continuation>();output->context=context;output->paragraph=para->desc.nmp;return 0;
    }
    bool split=!bottomless&&floating&&floater_output&&data->break_record&&finite.kstop==fmtrBrokenOutOfSpace;
    if(data->break_record&&!split)return PTS_tserrNotImplemented;
    if(!bottomless&&finite.kstop==fmtrNoProgressOutOfSpace) {
        if(details.pfsFloaterContent||width<0||height<0)return PTS_tserrInvalidClientOutput;
        output=std::make_shared<Continuation>();output->context=context;output->paragraph=para->desc.nmp;
        return 0;
    }
    if(!bottomless&&finite.kstop!=fmtrGoalReached&&!split)return PTS_tserrNotImplemented;
    if(width<0||height<0||width>rect.du||int64_t(rect.v)+top+height>INT32_MAX)return PTS_tserrInvalidClientOutput;
    if(!bottomless&&int64_t(top)+height>rect.dv&&!finite.fForcedProgress)return PTS_tserrInvalidClientOutput;
    int left=rect.u;
    if(floating&&props.fskfloatalignment==fskfloatalignMax)left+=rect.du-width;
    if(floating&&props.fskfloatalignment==fskfloatalignCenter)left+=(rect.du-width)/2;
    details.fsrcFloater={left,rect.v+top,width,height};
    if(box.fDefined) {
        if(int64_t(box.fsrc.u)+left<INT32_MIN||int64_t(box.fsrc.u)+left>INT32_MAX||
           int64_t(box.fsrc.v)+rect.v+top<INT32_MIN||int64_t(box.fsrc.v)+rect.v+top>INT32_MAX)return PTS_tserrInvalidClientOutput;
        box.fsrc.u+=left;box.fsrc.v+=rect.v+top;
    }
    para->desc.fsbbox=box;para->desc.pfspara=data.get();para->desc.dvrTopSpace=top;
    para->forced_progress=!bottomless&&finite.fForcedProgress;
    para->desc.dvrUsed=int(std::max<int64_t>(0,bottomless?int64_t(top)+height:std::min<int64_t>(int64_t(top)+height,rect.dv)));
    status=reinterpret_cast<PtsGetMCSClientAfterFloater>(cb.pfnGetMCSClientAfterFloater)(client,para->desc.pfsparaclient,para->desc.nmp,direction,input_margin,margin_output);if(status)return status;
    if(split){auto record=std::make_shared<FloaterBreakData>(context);record->callbacks=cb;record->handle=data->break_record;data->break_record=nullptr;*floater_output=record;}
    {std::lock_guard<std::mutex> lock(gate);floaters[data.get()]=data;}
    return 0;
}

struct SpanRowInput {void* name=nullptr;FSTABLEROWPROPS props{};std::vector<void*> names;std::vector<FSTABLEKCELLMERGE> merges;};
int format_span_group(const std::shared_ptr<ParaData>& para,const std::shared_ptr<TableData>& data,uint32_t direction,const FSRECT& rect,
    int top,int& used,bool bottomless,bool empty_ok,bool first_table,SpanRowInput first,void*& next,int& found,const std::shared_ptr<Continuation>& incoming,std::shared_ptr<Continuation>& output) {
    auto context=para->context;void* client=context->info.pfsclient;auto& fetch=data->callbacks.tablecbkfetch;auto& cb=data->callbacks.tablecbkcell;
    if(!cb.pfnFormatCellBottomless)return PTS_tserrNotImplemented;
    struct Master {void* name;size_t first,last;std::shared_ptr<TableCellData> cell;};
    std::vector<SpanRowInput> inputs;std::vector<Master> masters;std::unordered_map<void*,size_t> active;std::unordered_set<void*> seen_rows;
    inputs.push_back(std::move(first));
    for(size_t r=0;r<inputs.size();++r){
        auto& input=inputs[r];if(!input.name||!seen_rows.insert(input.name).second)return PTS_tserrInvalidClientOutput;
        std::unordered_set<void*> row_names;
        for(size_t i=0;i<input.names.size();++i){void* name=input.names[i];auto merge=input.merges[i];if(!name||!row_names.insert(name).second)return PTS_tserrInvalidClientOutput;
            if(merge==fskcellmergeNo||merge==fskcellmergeFirst){if(active.count(name))return PTS_tserrInvalidClientOutput;masters.push_back({name,r,r,nullptr});if(merge==fskcellmergeFirst)active[name]=masters.size()-1;}
            else if(merge==fskcellmergeMiddle||merge==fskcellmergeLast){auto at=active.find(name);if(at==active.end())return PTS_tserrInvalidClientOutput;masters[at->second].last=r;if(merge==fskcellmergeLast)active.erase(at);}
            else return PTS_tserrInvalidClientOutput;
        }
        int status=reinterpret_cast<PtsGetNextRow>(fetch.pfnGetNextRow)(client,para->desc.nmp,input.name,&found,&next);if(status)return status;
        if(active.empty())break;if(!found||!next)return PTS_tserrInvalidClientOutput;
        SpanRowInput added;added.name=next;status=reinterpret_cast<PtsGetRowProperties>(fetch.pfnGetRowProperties)(client,next,direction,&added.props);if(status)return status;
        if(added.props.cCells<0)return PTS_tserrInvalidClientOutput;
        added.names.resize(added.props.cCells);added.merges.resize(added.props.cCells);status=reinterpret_cast<PtsGetCells>(fetch.pfnGetCells)(client,next,added.props.cCells,added.names.data(),added.merges.data());if(status)return status;inputs.push_back(std::move(added));
    }
    std::vector<int> heights(inputs.size()),above(inputs.size()),below(inputs.size());
    for(size_t r=0;r<inputs.size();++r){auto& props=inputs[r].props;
        if(props.fskrowheight!=fskrowheightNatural&&!(props.fskrowheight==fskrowheightExactNoBreak&&props.dvrRowHeightRestriction==0))return PTS_tserrNotImplemented;
        above[r]=r==0&&data->rows.empty()?(first_table?props.dvrAboveTopRow:props.dvrAboveRowBreak):props.dvrAboveRow;
        below[r]=r+1==inputs.size()&&!found?props.dvrBelowBottomRow:props.dvrBelowRow;
        if(above[r]<0||below[r]<0)return PTS_tserrInvalidClientOutput;
    }
    bool resumed=incoming&&!incoming->span_cells.empty();
    if(resumed&&incoming->span_cells.size()!=masters.size())return PTS_tserrInvalidBreakRecord;
    auto format_cells=[&](bool finite,int available)->int {
        std::fill(heights.begin(),heights.end(),0);
        std::unordered_set<size_t> ordinary_rows;for(size_t i=0;i<masters.size();++i)if(masters[i].first==masters[i].last&&!(resumed&&incoming->span_cells[i].done))ordinary_rows.insert(masters[i].first);
        int row_available=ordinary_rows.empty()?available:available/int(ordinary_rows.size());
        std::vector<size_t> order;for(size_t i=0;i<masters.size();++i)if(masters[i].first==masters[i].last)order.push_back(i);for(size_t i=0;i<masters.size();++i)if(masters[i].first!=masters[i].last)order.push_back(i);
        for(size_t i:order){auto& master=masters[i];
            if(resumed&&incoming->span_cells[i].name!=master.name)return PTS_tserrInvalidBreakRecord;
            if(resumed&&incoming->span_cells[i].done){master.cell.reset();continue;}
            auto cell=std::make_shared<TableCellData>(context);cell->callbacks=cb;cell->name=master.name;CallbackTracks pending;int status=0;
            if(finite){int cell_available=master.first==master.last?row_available:available;
                if(master.first!=master.last){int64_t outside=0,inside_spacing=0;for(size_t r=0;r<inputs.size();++r){if(r<master.first||r>master.last)outside+=heights[r];if(r>=master.first&&r<master.last)inside_spacing+=int64_t(below[r])+above[r+1];}cell_available=int(std::max<int64_t>(0,int64_t(available)+inside_spacing-outside));}
                FSFMTR formatted{};void* record=resumed&&incoming->span_cells[i].record?incoming->span_cells[i].record->handle:nullptr;
                status=reinterpret_cast<PtsFormatCellFinite>(cb.pfnFormatCellFinite)(client,para->desc.pfsparaclient,record,master.name,nullptr,0,direction,0,cell_available,&formatted,&cell->handle,&cell->record,&cell->height);
                if(!status&&formatted.kstop!=fmtrGoalReached&&!(formatted.kstop==fmtrBrokenOutOfSpace&&cell->record))status=PTS_tserrNotImplemented;
            }else {FSFMTRBL formatted{};status=reinterpret_cast<PtsFormatCellBottomless>(cb.pfnFormatCellBottomless)(client,para->desc.pfsparaclient,master.name,direction,&formatted,&cell->handle,&cell->height);if(!status&&formatted!=fmtrblGoalReached)status=PTS_tserrNotImplemented;}
            pending.claimed_pages=cell->handle!=nullptr;pending.claimed_record=cell->record;if(pending.claimed_pages)for(const auto& page:pending.created_pages)page->inherit_updates=true;
            if(status)return status;if(!cell->handle||cell->height<0)return PTS_tserrInvalidClientOutput;
            master.cell=cell;if(master.first==master.last)heights[master.first]=std::max(heights[master.first],cell->height);
        }
        return 0;
    };
    auto constrain=[&]()->int {
        for(const auto& master:masters)if(master.cell&&master.first!=master.last){int64_t span=0;for(size_t r=master.first;r<=master.last;++r){span+=heights[r];if(r<master.last)span+=int64_t(below[r])+above[r+1];}
            if(span<master.cell->height){int64_t value=int64_t(heights[master.last])+master.cell->height-span;if(value>INT32_MAX)return PTS_tserrInvalidGeometry;heights[master.last]=int(value);}
        }
        return 0;
    };
    int64_t spacing=0;for(size_t r=0;r<inputs.size();++r)spacing+=int64_t(above[r])+below[r];
    int available=int(std::max<int64_t>(0,int64_t(rect.dv)-top-used-spacing));
    int status=format_cells(resumed,available);if(status)return status;status=constrain();if(status)return status;
    auto extent=[&](){int64_t value=spacing;for(int height:heights)value+=height;return value;};
    int64_t total=extent();
    if(!bottomless&&int64_t(top)+used+total>rect.dv){
        if(empty_ok||!data->rows.empty()){
            output=std::make_shared<Continuation>();output->context=context;output->paragraph=para->desc.nmp;output->table_row=inputs.front().name;if(resumed)output->span_cells=incoming->span_cells;
            if(!data->rows.empty())data->rows.back()->details.fskboundaryBelow=fsktablerowboundaryBreak;return 0;
        }
        if(resumed)return PTS_tserrNotImplemented;
        for(auto& master:masters)master.cell.reset();
        status=format_cells(true,available);if(status)return status;status=constrain();if(status)return status;total=extent();
        if(int64_t(top)+used+total>rect.dv)return PTS_tserrNotImplemented;
    }
    if(int64_t(rect.v)+top+used+total>INT32_MAX)return PTS_tserrInvalidGeometry;
    bool split=std::any_of(masters.begin(),masters.end(),[](const Master& master){return master.cell&&master.cell->record;});
    for(size_t i=0;i<masters.size();++i){const auto& master=masters[i];if(!master.cell)continue;int64_t span=0;for(size_t r=master.first;r<=master.last;++r){span+=heights[r];if(r<master.last)span+=int64_t(below[r])+above[r+1];}if(span>INT32_MAX)return PTS_tserrInvalidGeometry;
        int status=reinterpret_cast<PtsSetCellHeight>(cb.pfnSetCellHeight)(master.cell->handle,para->desc.pfsparaclient,resumed&&incoming->span_cells[i].record?incoming->span_cells[i].record->handle:nullptr,master.name,master.cell->record!=nullptr,direction,int(span));if(status)return status;
    }
    for(size_t r=0;r<inputs.size();++r){auto row=std::make_shared<TableRowData>(context);auto& input=inputs[r];row->description.fsnmRow=input.name;row->description.pfstablerow=row.get();row->description.fsupdinf.fskupd=fskupdNew;row->description.u.dvrRow=above[r]+heights[r]+below[r];
        row->details.fskboundaryAbove=resumed?fsktablerowboundaryBreak:data->rows.empty()?(first_table?fsktablerowboundaryOuter:fsktablerowboundaryBreak):fsktablerowboundaryInner;
        row->details.fskboundaryBelow=split?fsktablerowboundaryBreak:r+1==inputs.size()&&!found?fsktablerowboundaryOuter:fsktablerowboundaryInner;row->details.dvrAbove=above[r];row->details.dvrBelow=below[r];row->details.cCells=int(input.names.size());row->merges=input.merges;
        for(void* name:input.names){auto master=std::find_if(masters.begin(),masters.end(),[&](const Master& value){return value.name==name&&value.first<=r&&value.last>=r;});if(master==masters.end())return PTS_tserrInvalidClientOutput;row->cells.push_back(master->cell);}
        data->rows.push_back(row);
    }
    used+=int(total);
    if(split){output=std::make_shared<Continuation>();output->context=context;output->paragraph=para->desc.nmp;output->table_row=inputs.front().name;
        for(auto& master:masters){PendingTableCell pending{master.name,nullptr,!master.cell||!master.cell->record};if(master.cell&&master.cell->record){auto record=std::make_shared<CellBreakData>(context);record->callbacks=cb;record->handle=master.cell->record;master.cell->record=nullptr;pending.record=record;}output->span_cells.push_back(pending);}
    }
    return 0;
}
int format_table(const std::shared_ptr<ParaData>& para,uint32_t direction,const FSRECT& rect,void* input_margin,
    bool bottomless,bool empty_ok,int suppress,const std::shared_ptr<Continuation>& input,std::shared_ptr<Continuation>& output,void** margin_output) {
    auto context=para->context;void* client=context->info.pfsclient;auto& obj=context->info.fscbk.cbkobj;
    if(!obj.pfnGetObjectHandlerInfo||!obj.pfnConvertMcsclient)return PTS_tserrInvalidParameter;
    TableHandler handler{};int status=reinterpret_cast<PtsGetObjectHandlerInfo>(obj.pfnGetObjectHandlerInfo)(client,3,&handler);if(status)return status;
    if(handler.magic!=table_magic)return PTS_tserrInvalidClientOutput;
    auto& cb=handler.init;auto& fetch=cb.tablecbkfetch;auto& cells=cb.tablecbkcell;auto& table=cb.tableobjcbk;
    if(!table.pfnGetTableProperties||!table.pfnAutofitTable||!table.pfnGetMCSClientAfterTable||!fetch.pfnGetFirstRow||!fetch.pfnGetNextRow||!fetch.pfnGetRowProperties||!fetch.pfnGetCells||!cells.pfnDestroyCell||!cells.pfnDestroyCellBreakRecord||!cells.pfnSetCellHeight||!(bottomless?cells.pfnFormatCellBottomless:cells.pfnFormatCellFinite))return PTS_tserrInvalidParameter;
    FSTABLEOBJPROPS props{};status=reinterpret_cast<PtsGetTableProperties>(table.pfnGetTableProperties)(client,para->desc.nmp,direction,&props);if(status)return status;
    if(props.fFloat||props.fskclear!=fskclearNone||props.fswdirTable!=direction||props.ktablealignment!=fsktableobjAlignLeft||props.dvrCaptionTop||props.dvrCaptionBottom||props.durCaptionLeft||props.durCaptionRight)return PTS_tserrNotImplemented;
    for(int which=0;which<2;++which){void* callback=which?fetch.pfnGetFirstFooterRow:fetch.pfnGetFirstHeaderRow;if(callback){int found=0;void* name=nullptr;status=reinterpret_cast<PtsGetFirstHeaderRow>(callback)(client,para->desc.nmp,0,&found,&name);if(status)return status;if(found)return PTS_tserrNotImplemented;}}
    if(fetch.pfnGetDistributionKind){FSKTABLEHEIGHTDISTRIBUTION distribution{};status=reinterpret_cast<PtsGetDistributionKind>(fetch.pfnGetDistributionKind)(client,para->desc.nmp,direction,&distribution);if(status)return status;if(distribution!=fskdistributeUnchanged)return PTS_tserrNotImplemented;}
    int width=0,top=0;status=reinterpret_cast<PtsAutofitTable>(table.pfnAutofitTable)(client,para->desc.pfsparaclient,para->desc.nmp,direction,rect.du,&width);if(status)return status;
    if(width<0||int64_t(rect.u)+width>INT32_MAX)return PTS_tserrInvalidClientOutput;
    status=reinterpret_cast<PtsConvertMcsclient>(obj.pfnConvertMcsclient)(client,para->desc.pfsparaclient,para->desc.nmp,direction,input_margin,suppress,&top);if(status)return status;
    if(top<0||int64_t(rect.v)+top>INT32_MAX)return PTS_tserrInvalidGeometry;
    auto data=std::make_shared<TableData>(context);data->callbacks=cb;para->table=data;
    int found=1;void* name=input?input->table_row:nullptr;bool first_table=!name;
    if(!name){status=reinterpret_cast<PtsGetFirstRow>(fetch.pfnGetFirstRow)(client,para->desc.nmp,&found,&name);if(status)return status;}
    int used=0;std::unordered_set<void*> visited;
    while(found){
        if(!name||!visited.insert(name).second)return PTS_tserrInvalidClientOutput;
        void* next=nullptr;int has_next=0;status=reinterpret_cast<PtsGetNextRow>(fetch.pfnGetNextRow)(client,para->desc.nmp,name,&has_next,&next);if(status)return status;if(has_next&&!next)return PTS_tserrInvalidClientOutput;
        FSTABLEROWPROPS row_props{};status=reinterpret_cast<PtsGetRowProperties>(fetch.pfnGetRowProperties)(client,name,direction,&row_props);if(status)return status;
        if(row_props.cCells<0||(row_props.cCells>0&&width==0))return PTS_tserrInvalidClientOutput;
        int above=data->rows.empty()?(first_table?row_props.dvrAboveTopRow:row_props.dvrAboveRowBreak):row_props.dvrAboveRow;
        int below=has_next?row_props.dvrBelowRow:row_props.dvrBelowBottomRow;
        if(above<0||below<0||int64_t(used)+above+below+top>INT32_MAX)return PTS_tserrInvalidClientOutput;
        bool resumed=input&&data->rows.empty()&&input->table_row==name&&!input->table_cells.empty();
        below=std::max(below,row_props.dvrBelowRowBreak);
        int available=std::max(0,rect.dv-top-used-above-below),height=0;bool defer=false,split=false,row_forced=false;bool row_empty_ok=empty_ok||!data->rows.empty();
        auto row=std::make_shared<TableRowData>(context);row->description.fsnmRow=name;row->description.pfstablerow=row.get();row->description.fsupdinf.fskupd=fskupdNew;
        std::vector<void*> names(row_props.cCells);std::vector<FSTABLEKCELLMERGE> merge(row_props.cCells);
        status=reinterpret_cast<PtsGetCells>(fetch.pfnGetCells)(client,name,row_props.cCells,names.data(),merge.data());if(status)return status;
        if(std::any_of(merge.begin(),merge.end(),[](FSTABLEKCELLMERGE value){return value!=fskcellmergeNo;})){
            if(resumed)return PTS_tserrNotImplemented;
            SpanRowInput group{name,row_props,names,merge};status=format_span_group(para,data,direction,rect,top,used,bottomless,empty_ok,first_table,std::move(group),next,has_next,input&&input->table_row==name?input:nullptr,output);if(status)return status;
            if(output)break;name=next;found=has_next;continue;
        }
        if(row_props.fskrowheight!=fskrowheightNatural&&!(row_props.cCells==0&&row_props.fskrowheight==fskrowheightExactNoBreak&&row_props.dvrRowHeightRestriction==0))return PTS_tserrNotImplemented;
        if(resumed&&input->table_cells.size()!=names.size())return PTS_tserrInvalidBreakRecord;
        for(int i=0;i<row_props.cCells;++i){
            if(!names[i]||merge[i]!=fskcellmergeNo)return PTS_tserrNotImplemented;
            if(resumed&&input->table_cells[i].name!=names[i])return PTS_tserrInvalidBreakRecord;
            if(resumed&&input->table_cells[i].done){row->cells.push_back(nullptr);continue;}
            void* cell_input=resumed&&input->table_cells[i].record?input->table_cells[i].record->handle:nullptr;
            auto cell=std::make_shared<TableCellData>(context);cell->callbacks=cells;cell->name=names[i];CallbackTracks pending;
            if(bottomless){FSFMTRBL formatted{};status=reinterpret_cast<PtsFormatCellBottomless>(cells.pfnFormatCellBottomless)(client,para->desc.pfsparaclient,names[i],direction,&formatted,&cell->handle,&cell->height);if(!status&&formatted!=fmtrblGoalReached)status=PTS_tserrNotImplemented;}
            else {FSFMTR formatted{};status=reinterpret_cast<PtsFormatCellFinite>(cells.pfnFormatCellFinite)(client,para->desc.pfsparaclient,cell_input,names[i],nullptr,row_empty_ok,direction,0,available,&formatted,&cell->handle,&cell->record,&cell->height);
                row_forced=row_forced||formatted.fForcedProgress;
                if(!status&&(cell->record||formatted.kstop!=fmtrGoalReached)){
                    if(row_empty_ok)defer=true;
                    else if(cell->record&&formatted.kstop==fmtrBrokenOutOfSpace)split=true;
                    else status=PTS_tserrNotImplemented;
                }
            }
            // The callback reports a managed CellParaClient handle. Its original
            // DestroyCell callback owns the native subpage(s) created inside it.
            pending.claimed_pages=cell->handle!=nullptr;pending.claimed_record=cell->record;
            if(pending.claimed_pages)for(const auto& page:pending.created_pages)page->inherit_updates=true;
            if(status)return status;if(defer)break;
            if(!cell->handle||cell->height<0)return PTS_tserrInvalidClientOutput;
            height=std::max(height,cell->height);row->cells.push_back(cell);
        }
        if(int64_t(rect.v)+top+used+above+height+below>INT32_MAX)return PTS_tserrInvalidGeometry;
        if(!bottomless&&int64_t(top)+used+above+height+below>rect.dv){if(row_empty_ok)defer=true;else if(!row_forced)return PTS_tserrNotImplemented;}
        if(defer){output=std::make_shared<Continuation>();output->context=context;output->paragraph=para->desc.nmp;output->table_row=name;if(resumed)output->table_cells=input->table_cells;if(!data->rows.empty()){auto& last=data->rows.back();last->details.fskboundaryBelow=fsktablerowboundaryBreak;}break;}
        for(size_t i=0;i<row->cells.size();++i){const auto& cell=row->cells[i];if(!cell)continue;
            void* cell_input=resumed&&input->table_cells[i].record?input->table_cells[i].record->handle:nullptr;
            status=reinterpret_cast<PtsSetCellHeight>(cells.pfnSetCellHeight)(cell->handle,para->desc.pfsparaclient,cell_input,cell->name,cell->record!=nullptr,direction,height);if(status)return status;
        }
        row->details.fForcedRow=row_forced;para->forced_progress=para->forced_progress||row_forced;
        row->description.u.dvrRow=above+height+below;row->details.fskboundaryAbove=data->rows.empty()?(first_table?fsktablerowboundaryOuter:fsktablerowboundaryBreak):fsktablerowboundaryInner;
        row->details.fskboundaryBelow=split?fsktablerowboundaryBreak:has_next?fsktablerowboundaryInner:fsktablerowboundaryOuter;row->details.dvrAbove=above;row->details.dvrBelow=below;row->details.cCells=int(row->cells.size());
        used+=row->description.u.dvrRow;data->rows.push_back(row);
        if(split){
            output=std::make_shared<Continuation>();output->context=context;output->paragraph=para->desc.nmp;output->table_row=name;
            for(size_t i=0;i<row->cells.size();++i){const auto& cell=row->cells[i];PendingTableCell pending{names[i],nullptr,!cell||!cell->record};
                if(cell&&cell->record){auto record=std::make_shared<CellBreakData>(context);record->callbacks=cells;record->handle=cell->record;cell->record=nullptr;pending.record=record;}
                output->table_cells.push_back(pending);
            }
            break;
        }
        name=next;found=has_next;
    }
    data->details.fsnmTable=para->desc.nmp;data->details.fsrcTableObj={rect.u,rect.v+top,width,used};data->details.fswdirTable=direction;data->details.fskupdTableProper=fskupdNew;data->details.pfstableProper=data.get();data->proper={used,int(data->rows.size())};
    para->desc.pfspara=data.get();para->desc.dvrTopSpace=top;para->desc.dvrUsed=data->rows.empty()&&output?0:para->forced_progress?std::min(top+used,rect.dv):top+used;
    if(used)para->desc.fsbbox={1,data->details.fsrcTableObj};
    if(!output){status=reinterpret_cast<PtsGetMCSClientAfterTable>(table.pfnGetMCSClientAfterTable)(client,para->desc.pfsparaclient,para->desc.nmp,direction,input_margin,margin_output);if(status)return status;}
    {std::lock_guard<std::mutex> lock(gate);tables[data.get()]=data;for(const auto& row:data->rows)table_rows[row.get()]=row;}
    return 0;
}
int format_track(const std::shared_ptr<Context>& context,void* segment,uint32_t direction,const FSRECT& rect,
    int suppress,void* incoming,std::shared_ptr<TrackData>& result,void** margin_output,bool bottomless=false,const std::shared_ptr<Continuation>& input={},bool empty_ok=false,bool previous_page=true,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA suppress_break=fsksuppresshardbreakbeforefirstparaPageAndColumn,bool keep_self=false) {
    auto& cb=context->info.fscbk.cbkgen;void* client=context->info.pfsclient;
    if(!segment||(direction!=PTS_fswdirES&&direction!=PTS_fswdirWS)||rect.du<=0||rect.dv<0||int64_t(rect.u)+rect.du>INT32_MAX||int64_t(rect.v)+rect.dv>INT32_MAX)return PTS_tserrNotImplemented;
    if(!cb.pfnGetFirstPara||!cb.pfnGetNextPara||!cb.pfnGetParaProperties||!cb.pfnCreateParaclient||!cb.pfnDestroyParaclient||!context->info.fscbk.cbkobj.pfnDestroyMcsclient)return PTS_tserrInvalidParameter;
    auto track=std::make_shared<TrackData>(context);track->segment=segment;track->direction=direction;track->rect=rect;track->bottomless=bottomless;
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
        if(props.idobj!=-1&&props.idobj!=0&&props.idobj!=1&&props.idobj!=2&&props.idobj!=3)return PTS_tserrNotImplemented;
        bool first=track->paras.empty();
        bool continued=input&&first&&(input->child||input->table_row||input->dcp>0||input->text_ended||!input->floaters.empty());
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
        auto para=std::make_shared<ParaData>();para->context=context;para->direction=direction;para->keep_with_next=props.fKeepWithNext!=0;para->desc.nmp=name;para->desc.idobj=props.idobj;para->desc.fsupdinf.fskupd=fskupdNew;
        status=reinterpret_cast<PtsCreateParaclient>(cb.pfnCreateParaclient)(client,name,&para->desc.pfsparaclient);if(status)return status;
        if(!para->desc.pfsparaclient)return PTS_tserrInvalidClientOutput;
        FSRECT available=rect;available.v+=track->used;available.dv-=track->used;
        std::shared_ptr<Continuation> next_position;FSFMTRKSTOP next_stop=fmtrBrokenOutOfSpace;
        auto resume=(input&&track->paras.empty())?input:std::shared_ptr<Continuation>{};
        if(props.idobj==-1)status=format_text(para,direction,available,resume,next_position,bottomless,!empty_ok&&track->paras.empty(),keep_self||props.fKeepWithNext,previous_page);
        else if(props.idobj==3) {
            void* margin_next=nullptr;
            status=format_table(para,direction,available,input_margin,bottomless,empty_ok||!track->paras.empty(),suppress&&first,resume,next_position,&margin_next);
            if(!next_position){margin.reset();margin.value=margin_next;input_margin=margin.value;}
            else if(margin_next)reinterpret_cast<PtsDestroyMcsclient>(context->info.fscbk.cbkobj.pfnDestroyMcsclient)(client,margin_next);
        }
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
        bool source_progress=(para->text&&!para->text->lines.empty())||(para->table&&!para->table->rows.empty());
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
void clear_track(const std::shared_ptr<TrackData>& track,bool inherited=false) {
    auto update=inherited?fskupdInherited:fskupdNoChange;track->update=update;
    for(const auto& para:track->paras) {
        para->desc.fsupdinf.fskupd=update;
        if(para->floater)para->floater->details.fskupdContent=update;
        if(para->table){para->table->details.fskupdTableProper=update;for(const auto& row:para->table->rows){row->description.fsupdinf.fskupd=update;for(const auto& cell:row->cells){if(!cell)continue;cell->update=update;if(cell->callbacks.pfnClearUpdateInfoInCell)reinterpret_cast<PtsClearUpdateInfoInCell>(cell->callbacks.pfnClearUpdateInfoInCell)(cell->handle);}}}
        if(para->text)for(const auto& object:para->text->attached){object.para->desc.fsupdinf.fskupd=update;if(object.para->floater)object.para->floater->details.fskupdContent=update;for(const auto& figure_page:object.para->figure_pages)clear_subpage(figure_page);auto content=object.para->floater?lookup_owned(pages,track->context.get(),object.para->floater->details.pfsFloaterContent):nullptr;if(content&&content->subpage){content->inherit_updates=content->inherit_updates||inherited;clear_subpage(content);}}
        if(para->desc.idobj==0){auto child=lookup_owned(tracks,track->context.get(),para->desc.pfspara);if(child)clear_track(child,inherited);}
        if(para->desc.idobj==1){auto child=lookup_owned(pages,track->context.get(),para->desc.pfspara);if(child&&child->subpage){child->inherit_updates=child->inherit_updates||inherited;clear_subpage(child);}}
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
struct BalancingInfo {
    int64_t lines=0,height=0;int minimum=0;bool has_height=false;
    int add(int count,int sum,int step) {
        if(count<0||sum<0||step<0||(!count&&(sum||step))||step>sum)return PTS_tserrInvalidClientOutput;
        lines+=count;height+=sum;if(lines>INT32_MAX||height>INT32_MAX)return PTS_tserrInvalidGeometry;
        if(count){minimum=has_height?std::min(minimum,step):step;has_height=true;}return 0;
    }
};
int measure_track(const std::shared_ptr<TrackData>& track,BalancingInfo& result) {
    // Original installed object callbacks may reenter these exports for their
    // native child track/page. Bound malformed callback recursion.
    static thread_local int depth=0;if(depth>=128)return PTS_tserrInvalidClientOutput;
    ++depth;struct Pop {int& value;~Pop(){--value;}} pop{depth};
    for(const auto& para:track->paras) {
        int status=0;
        if(para->text){for(const auto& line:para->text->lines){int64_t height=int64_t(line.dvrAscent)+line.dvrDescent;if(height>INT32_MAX)return PTS_tserrInvalidGeometry;status=result.add(1,int(height),int(height));if(status)return status;}}
        else if(para->desc.idobj==0||para->desc.idobj==1){auto fn=track->context->installed->methods[para->desc.idobj].pfnGetColumnBalancingInfo;if(!fn)return PTS_tserrInvalidParameter;
            int count=0,sum=0,minimum=0;status=reinterpret_cast<PtsObjGetColumnBalancingInfo>(fn)(para->desc.pfspara,para->direction,&count,&sum,&minimum);if(status)return status;status=result.add(count,sum,minimum);if(status)return status;}
        else if(para->floater){auto& value=*para->floater;auto fn=value.callbacks.pfnGetFloaterContentColumnBalancingInfo;if(!fn)return PTS_tserrInvalidParameter;
            int count=0,sum=0,minimum=0;status=reinterpret_cast<PtsGetFloaterContentColumnBalancingInfo>(fn)(value.details.pfsFloaterContent,para->direction,&count,&sum,&minimum);if(status)return status;status=result.add(count,sum,minimum);if(status)return status;}
        else if(para->table){for(const auto& row:para->table->rows){int height=row->description.u.dvrRow;status=result.add(height?1:0,height,height);if(status)return status;}}
        else return PTS_tserrNotImplemented;
    }
    return 0;
}
int measure_page(const std::shared_ptr<PageData>& page,BalancingInfo& result) {
    if(page->section){for(const auto& track:page->section->columns){int status=measure_track(track,result);if(status)return status;}return 0;}
    return measure_track(page->track,result);
}
int required_page_height(const std::shared_ptr<PageData>& page,int& height,int depth);
int required_track_height(const std::shared_ptr<TrackData>& track,int& height,int depth) {
    if(depth>=128)return PTS_tserrInvalidClientOutput;int64_t chain=0;
    for(const auto& para:track->paras) {
        if(para->keep_together)height=std::max(height,para->desc.dvrUsed);
        if(para->keep_with_next){chain+=para->desc.dvrUsed;if(chain>INT32_MAX)return PTS_tserrInvalidGeometry;}
        else if(chain){int64_t following=para->desc.dvrUsed;
            if(para->text&&!para->keep_together){following=para->desc.dvrTopSpace;size_t count=std::min(para->text->lines.size(),size_t(para->min_lines_before_break));for(size_t i=0;i<count;++i)following+=int64_t(para->text->lines[i].dvrAscent)+para->text->lines[i].dvrDescent;}
            chain+=following;if(chain>INT32_MAX)return PTS_tserrInvalidGeometry;height=std::max(height,int(chain));chain=0;}
        int status=0;
        if(para->desc.idobj==0){auto child=lookup_owned(tracks,track->context.get(),para->desc.pfspara);if(child)status=required_track_height(child,height,depth+1);}
        if(para->desc.idobj==1){auto child=lookup_owned(pages,track->context.get(),para->desc.pfspara);if(child)status=required_page_height(child,height,depth+1);}
        if(status)return status;
    }
    return 0;
}
int required_page_height(const std::shared_ptr<PageData>& page,int& height,int depth) {
    if(page->section){for(const auto& track:page->section->columns){int status=required_track_height(track,height,depth);if(status)return status;}return 0;}
    return required_track_height(page->track,height,depth);
}
template<class Format> int balance_columns(std::shared_ptr<PageData>& page,int height,MarginOwner& margin,Format format) {
    // A full page with continuation keeps its capacity. Balance the completed
    // final section only, never discard source to make a shorter trial fit.
    if(height<=1||!page->section||page->continuation||page->forced_progress)return 0;
    int required=0;int status=required_page_height(page,required,0);if(status)return status;
    // A trial must not make an originally fitting keep-together paragraph or
    // keep-with-next chain oversized and then split it through the progress rule.
    int low=std::max(0,std::min(height-1,required-1)),high=height;
    while(high-low>1) {
        int next=low+(high-low)/2;std::shared_ptr<PageData> trial;MarginOwner trial_margin{page->context};
        int status=format(next,trial,&trial_margin.value);if(status)return status;
        if(!trial->continuation&&!trial->forced_progress){high=next;page=std::move(trial);margin.reset();margin.value=trial_margin.value;trial_margin.value=nullptr;}
        else low=next;
    }
    return 0;
}
int build_page(const std::shared_ptr<Context>& context,void* section,bool bottomless,std::shared_ptr<PageData>& result,const std::shared_ptr<Continuation>& input={},int height_limit=-1) {
    auto& cb=context->info.fscbk.cbkgen;void* client=context->info.pfsclient;
    if(!section||!cb.pfnGetPageDimensions||!cb.pfnGetSectionProperties||!cb.pfnGetMainTextSegment)return PTS_tserrInvalidParameter;
    FSRECT rect{};uint32_t direction=0,section_direction=0;int header=0,width=0,height=0;
    int status=reinterpret_cast<PtsGetPageDimensions>(cb.pfnGetPageDimensions)(client,section,&direction,&header,&width,&height,&rect);if(status)return status;
    int new_page=0,balance=0,columns=0,segments=0,heights=0;
    status=reinterpret_cast<PtsGetSectionProperties>(cb.pfnGetSectionProperties)(client,section,&new_page,&section_direction,&balance,&columns,&segments,&heights);if(status)return status;
    if((direction!=PTS_fswdirES&&direction!=PTS_fswdirWS)||section_direction!=direction||header||columns<1||segments||heights||(balance&&bottomless)|| (bottomless&&columns!=1))return PTS_tserrNotImplemented;
    if(width<=0||(!bottomless&&height<=0)||rect.u<0||rect.v<0||rect.du<=0||int64_t(rect.u)+rect.du>width||(!bottomless&&int64_t(rect.v)+rect.dv>height))return PTS_tserrInvalidGeometry;
    if(direction==PTS_fswdirWS){FSRECT page_rect{0,0,width,height},converted{};status=FsTransformRectangle(PTS_fswdirES,&page_rect,&rect,direction,&converted);if(status)return status;rect=converted;}
    if(height_limit>=0)rect.dv=std::min(rect.dv,height_limit);
    if(bottomless)rect.dv=INT32_MAX-rect.v;
    FigureGeometry figure_geometry({0,0,width,height},rect);
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
        auto data=std::make_shared<SectionData>(context);page->section=data;data->direction=direction;
        auto& desc=data->description;desc.fsupdinf.fskupd=fskupdNew;desc.nms=section;desc.fsrc=rect;desc.pfssection=data.get();
        auto continuation=input;bool finished=false,stop_page=false;FSFMTRKSTOP stop=fmtrGoalReached;u=rect.u;
        for(int index=0;index<columns;++index) {
            u+=info[index].durBefore;FSRECT column{int(u),rect.v,info[index].durWidth,rect.dv};u+=column.du;
            std::shared_ptr<TrackData> track;MarginOwner column_margin{context};
            if(finished||stop_page){track=std::make_shared<TrackData>(context);track->segment=segment;track->direction=direction;track->rect=column;}
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
    if(balance&&height_limit<0){status=balance_columns(page,rect.dv,margin,[&](int limit,std::shared_ptr<PageData>& trial,void**){return build_page(context,section,bottomless,trial,input,limit);});if(status)return status;}
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
            if(para->text)for(const auto& object:para->text->attached){status=box(object.para->desc.fsbbox);if(status)return status;auto& rect=attachment_rect(object.para);status=add(rect.v,rect.dv);if(status)return status;if(object.para->figure){status=box(object.para->figure->details.fsbbox);if(status)return status;status=add(object.para->figure->details.fsptPosPreliminary.v,0);if(status)return status;}}
            if(para->desc.idobj==0){auto child=lookup_owned(tracks,track->context.get(),para->desc.pfspara);if(!child)return PTS_tserrInvalidQuery;status=collect(child);if(status)return status;}
            // Subpage content uses its own local coordinate system. Its outer
            // paragraph bbox moves here; its local tracks remain unchanged.
        }
        return 0;
    }
    void apply(){for(const auto& item:values)*item.first=item.second;for(const auto& track:owners){track->update=fskupdNew;for(const auto& para:track->paras){para->desc.fsupdinf.fskupd=fskupdNew;if(para->floater)para->floater->details.fskupdContent=fskupdNew;if(para->table){para->table->details.fskupdTableProper=fskupdNew;for(const auto& row:para->table->rows){row->description.fsupdinf.fskupd=fskupdNew;for(const auto& cell:row->cells)if(cell)cell->update=fskupdNew;}}if(para->text)for(const auto& object:para->text->attached){object.para->desc.fsupdinf.fskupd=fskupdNew;if(object.para->floater)object.para->floater->details.fskupdContent=fskupdNew;}}}}
};
}
extern "C" __declspec(dllexport) int WINAPI FsSynchronizeBottomlessSubtrack(void* context_handle,void* handle,void* geometry,uint32_t direction,int shift) {
    auto track=lookup_owned(tracks,context_handle,handle);
    if(!track||!track->subtrack||!track->bottomless)return PTS_tserrInvalidQuery;
    if(geometry||direction!=track->direction||(direction!=PTS_fswdirES&&direction!=PTS_fswdirWS))return PTS_tserrNotImplemented;
    try {TrackShift plan{shift};int status=plan.collect(track);if(status)return status;std::lock_guard<std::mutex> lock(gate);plan.apply();return 0;}
    catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
namespace {
int compare_tracks(const std::shared_ptr<TrackData>& old,const std::shared_ptr<TrackData>& fresh,int shift,bool& equal);
int compare_pages(const std::shared_ptr<PageData>& old,const std::shared_ptr<PageData>& fresh,bool& equal);
bool equal_rect(const FSRECT& a,const FSRECT& b,int shift) {
    return a.u==b.u&&int64_t(a.v)+shift==b.v&&a.du==b.du&&a.dv==b.dv;
}
bool equal_bbox(const FSBBOX& a,const FSBBOX& b,int shift) {
    return bool(a.fDefined)==bool(b.fDefined)&&(!a.fDefined||equal_rect(a.fsrc,b.fsrc,shift));
}
int compare_paras(const std::shared_ptr<ParaData>& old,const std::shared_ptr<ParaData>& fresh,int shift,bool& equal) {
    equal=false;const auto& a=old->desc;const auto& b=fresh->desc;
    if(a.nmp!=b.nmp||a.idobj!=b.idobj||a.dvrUsed!=b.dvrUsed||a.dvrTopSpace!=b.dvrTopSpace||!equal_bbox(a.fsbbox,b.fsbbox,shift))return 0;
    if(old==fresh){equal=true;return 0;}
    auto context=old->context;void* client=context->info.pfsclient;
    auto changed=context->info.fscbk.cbkgen.pfnUpdGetParaChange;
    if(!changed)return 0;FSKCHANGE change=fskchNew;int done=0;
    int status=reinterpret_cast<PtsUpdGetParaChange>(changed)(client,a.nmp,&change,&done);if(status)return status;
    if(change!=fskchNone)return 0;
    if(old->text&&fresh->text){const auto& x=*old->text;const auto& y=*fresh->text;
        if(x.direction!=y.direction||x.lines.size()!=y.lines.size()||x.attached.size()!=y.attached.size())return 0;
        for(size_t i=0;i<x.lines.size();++i){auto l=x.lines[i],r=y.lines[i];
            if(l.pfslineclient!=r.pfslineclient||l.dcpFirst!=r.dcpFirst||l.dcpLim!=r.dcpLim||l.urStart!=r.urStart||l.dur!=r.dur||int64_t(l.vrStart)+shift!=r.vrStart||l.dvrAscent!=r.dvrAscent||l.dvrDescent!=r.dvrDescent||l.urBBox!=r.urBBox||l.durBBox!=r.durBBox||l.fForceBroken!=r.fForceBroken||l.fTreatedAsFirst!=r.fTreatedAsFirst)return 0;
        }
        for(size_t i=0;i<x.attached.size();++i){if(x.attached[i].anchor!=y.attached[i].anchor)return 0;bool same=false;status=compare_paras(x.attached[i].para,y.attached[i].para,shift,same);if(status||!same)return status;}
        equal=true;return 0;
    }
    if(old->floater&&fresh->floater){auto& x=*old->floater;auto& y=*fresh->floater;if(!equal_rect(x.details.fsrcFloater,y.details.fsrcFloater,shift))return 0;
        if(x.details.pfsFloaterContent==y.details.pfsFloaterContent){equal=true;return 0;}
        if(!x.callbacks.pfnCompareFloaterContents)return 0;FSCOMPRESULT result=fscmprChangeInside;
        status=reinterpret_cast<PtsCompareFloaterContents>(x.callbacks.pfnCompareFloaterContents)(x.details.pfsFloaterContent,y.details.pfsFloaterContent,&result);equal=!status&&result==fscmprNoChange;return status;
    }
    if(old->figure&&fresh->figure){if(!equal_rect(old->figure->details.fsrcFlowAround,fresh->figure->details.fsrcFlowAround,shift)||old->figure_pages.size()!=fresh->figure_pages.size())return 0;
        for(size_t i=0;i<old->figure_pages.size();++i){bool same=false;status=compare_pages(old->figure_pages[i],fresh->figure_pages[i],same);if(status||!same)return status;}equal=true;return 0;
    }
    if(old->table&&fresh->table){auto& x=*old->table;auto& y=*fresh->table;
        if(!equal_rect(x.details.fsrcTableObj,y.details.fsrcTableObj,shift)||x.rows.size()!=y.rows.size())return 0;
        std::unordered_map<void*,void*> forward,reverse;
        for(size_t row=0;row<x.rows.size();++row){const auto& left=*x.rows[row];const auto& right=*y.rows[row];
            if(left.description.fsnmRow!=right.description.fsnmRow||left.description.u.dvrRow!=right.description.u.dvrRow||left.merges!=right.merges||left.cells.size()!=right.cells.size())return 0;
            for(size_t i=0;i<left.cells.size();++i){auto l=left.cells[i],r=right.cells[i];if(bool(l)!=bool(r))return 0;if(!l)continue;if(l->name!=r->name||l->height!=r->height)return 0;auto paired=forward.emplace(l.get(),r.get());if(!paired.second){if(paired.first->second!=r.get())return 0;continue;}if(!reverse.emplace(r.get(),l.get()).second)return 0;
                if(l==r)continue;if(!l->callbacks.pfnCompareCells)return 0;FSCOMPRESULT result=fscmprChangeInside;status=reinterpret_cast<PtsCompareCells>(l->callbacks.pfnCompareCells)(l->handle,r->handle,&result);if(status||result!=fscmprNoChange)return status;
            }
        }equal=true;return 0;
    }
    if(a.idobj==0||a.idobj==1){auto fn=context->installed->methods[a.idobj].pfnComparePara;if(!fn)return 0;FSCOMPRESULT result=fscmprChangeInside;int shifted=0;
        status=reinterpret_cast<PtsObjComparePara>(fn)(a.pfsparaclient,a.pfspara,b.pfsparaclient,b.pfspara,old->direction,&result,&shifted);
        equal=!status&&((result==fscmprNoChange&&(shift==0||a.idobj==1))||(result==fscmprShifted&&shifted==shift));return status;
    }
    return 0;
}
int compare_tracks(const std::shared_ptr<TrackData>& old,const std::shared_ptr<TrackData>& fresh,int shift,bool& equal) {
    equal=false;if(old==fresh){equal=true;return 0;}
    if(old->segment!=fresh->segment||old->bottomless!=fresh->bottomless||old->direction!=fresh->direction||old->used!=fresh->used||old->stop!=fresh->stop||old->forced_progress!=fresh->forced_progress||old->paras.size()!=fresh->paras.size()||!equal_rect(old->rect,fresh->rect,shift)||!equal_bbox(old->bbox,fresh->bbox,shift))return 0;
    for(size_t i=0;i<old->paras.size();++i){bool same=false;int status=compare_paras(old->paras[i],fresh->paras[i],shift,same);if(status||!same)return status;}equal=true;return 0;
}
int compare_pages(const std::shared_ptr<PageData>& old,const std::shared_ptr<PageData>& fresh,bool& equal) {
    equal=false;if(old==fresh){equal=true;return 0;}
    if(old->bottomless!=fresh->bottomless||old->stop!=fresh->stop||bool(old->section)!=bool(fresh->section))return 0;
    if(!old->section)return compare_tracks(old->track,fresh->track,0,equal);
    if(old->section->direction!=fresh->section->direction||old->section->columns.size()!=fresh->section->columns.size())return 0;
    for(size_t i=0;i<old->section->columns.size();++i){bool same=false;int status=compare_tracks(old->section->columns[i],fresh->section->columns[i],0,same);if(status||!same)return status;}equal=true;return 0;
}
int transfer_tracks(const std::shared_ptr<TrackData>& old,const std::shared_ptr<TrackData>& fresh);
int transfer_pages(const std::shared_ptr<PageData>& old,const std::shared_ptr<PageData>& fresh) {
    if(old==fresh)return 0;
    if(old->section&&fresh->section){size_t count=std::min(old->section->columns.size(),fresh->section->columns.size());for(size_t i=0;i<count;++i){int status=transfer_tracks(old->section->columns[i],fresh->section->columns[i]);if(status)return status;}}
    else if(!old->section&&!fresh->section)return transfer_tracks(old->track,fresh->track);
    return 0;
}
int transfer_para(const std::shared_ptr<ParaData>& old,const std::shared_ptr<ParaData>& fresh) {
    if(old==fresh)return 0;
    auto context=old->context;int status=0;
    if(old->desc.pfsparaclient!=fresh->desc.pfsparaclient&&fresh->transferred_from.lock()!=old){
        auto fn=context->info.fscbk.cbkgen.pfnTransferDisplayInfo;if(!fn)return PTS_tserrInvalidParameter;
        status=reinterpret_cast<PtsTransferDisplayInfo>(fn)(context->info.pfsclient,old->desc.pfsparaclient,fresh->desc.pfsparaclient);if(status)return status;fresh->transferred_from=old;
    }
    // Retain new-content flags: moved visuals must still rebuild changed glyphs.
    if(old->text&&fresh->text){for(const auto& next:fresh->text->attached){auto at=std::find_if(old->text->attached.begin(),old->text->attached.end(),[&](const AttachedData& value){return value.para->desc.nmp==next.para->desc.nmp&&value.para->desc.idobj==next.para->desc.idobj;});if(at!=old->text->attached.end()){status=transfer_para(at->para,next.para);if(status)return status;}}}
    if(old->figure&&fresh->figure){for(size_t i=0;i<std::min(old->figure_pages.size(),fresh->figure_pages.size());++i){status=transfer_pages(old->figure_pages[i],fresh->figure_pages[i]);if(status)return status;}}
    if(old->floater&&fresh->floater&&old->floater->callbacks.pfnTransferDisplayInfoInFloaterContent){status=reinterpret_cast<PtsTransferDisplayInfoInFloaterContent>(old->floater->callbacks.pfnTransferDisplayInfoInFloaterContent)(old->floater->details.pfsFloaterContent,fresh->floater->details.pfsFloaterContent);if(status)return status;}
    if(old->table&&fresh->table){std::unordered_set<void*> visited;for(const auto& row:fresh->table->rows)for(const auto& next:row->cells){if(!next||!visited.insert(next.get()).second)continue;std::shared_ptr<TableCellData> previous;
        for(const auto& before:old->table->rows)for(const auto& cell:before->cells)if(cell&&cell->name==next->name)previous=cell;
        if(previous&&previous->handle!=next->handle&&next->transferred_from.lock()!=previous&&previous->callbacks.pfnTransferDisplayInfoCell){status=reinterpret_cast<PtsTransferDisplayInfoCell>(previous->callbacks.pfnTransferDisplayInfoCell)(previous->handle,next->handle);if(status)return status;next->transferred_from=previous;}
    }}
    if(old->desc.idobj==0||old->desc.idobj==1){auto child=context->installed->methods[old->desc.idobj].pfnTransferDisplayInfoPara;if(child){status=reinterpret_cast<PtsObjTransferDisplayInfoPara>(child)(old->desc.pfspara,fresh->desc.pfspara);if(status)return status;}}
    return 0;
}
int transfer_tracks(const std::shared_ptr<TrackData>& old,const std::shared_ptr<TrackData>& fresh) {
    if(old==fresh)return 0;
    for(const auto& next:fresh->paras){auto at=std::find_if(old->paras.begin(),old->paras.end(),[&](const std::shared_ptr<ParaData>& value){return value->desc.nmp==next->desc.nmp&&value->desc.idobj==next->desc.idobj;});if(at!=old->paras.end()){int status=transfer_para(*at,next);if(status)return status;}}
    return 0;
}
}
extern "C" __declspec(dllexport) int WINAPI FsGetSubtrackColumnBalancingInfo(void* context,void* handle,uint32_t direction,int* lines,int* height,int* minimum) {
    if(!lines||!height||!minimum)return PTS_tserrNullOutputParameter;*lines=*height=*minimum=0;
    auto track=lookup_owned(tracks,context,handle);if(!track||!track->subtrack)return PTS_tserrInvalidQuery;if(track->direction!=direction)return PTS_tserrNotImplemented;
    try{BalancingInfo value;int status=measure_track(track,value);if(status)return status;*lines=int(value.lines);*height=int(value.height);*minimum=value.minimum;return 0;}catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsGetSubpageColumnBalancingInfo(void* context,void* handle,uint32_t* direction,int* lines,int* height,int* minimum) {
    if(!direction||!lines||!height||!minimum)return PTS_tserrNullOutputParameter;*direction=0;*lines=*height=*minimum=0;
    auto page=lookup_owned(pages,context,handle);if(!page||!page->subpage)return PTS_tserrInvalidQuery;
    try{BalancingInfo value;int status=measure_page(page,value);if(status)return status;*direction=page->section?page->section->direction:page->track->direction;*lines=int(value.lines);*height=int(value.height);*minimum=value.minimum;return 0;}catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsCompareSubtrack(void* context,void* previous,void* current,uint32_t direction,FSCOMPRESULT* result,int* shift) {
    if(!result||!shift)return PTS_tserrNullOutputParameter;*result=fscmprChangeInside;*shift=0;
    auto old=lookup_owned(tracks,context,previous),fresh=lookup_owned(tracks,context,current);if(!old||!fresh||!old->subtrack||!fresh->subtrack)return PTS_tserrInvalidQuery;
    if(direction!=old->direction||direction!=fresh->direction)return PTS_tserrNotImplemented;
    int64_t delta=int64_t(fresh->rect.v)-old->rect.v;if(delta<INT32_MIN||delta>INT32_MAX)return PTS_tserrInvalidGeometry;
    try {bool equal=false;int status=compare_tracks(old,fresh,int(delta),equal);if(status)return status;if(equal){*result=delta?fscmprShifted:fscmprNoChange;*shift=int(delta);}return 0;}catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsCompareSubpages(void* context,void* previous,void* current,FSCOMPRESULT* result) {
    if(!result)return PTS_tserrNullOutputParameter;*result=fscmprChangeInside;auto old=lookup_owned(pages,context,previous),fresh=lookup_owned(pages,context,current);if(!old||!fresh||!old->subpage||!fresh->subpage)return PTS_tserrInvalidQuery;
    try {bool equal=false;int status=compare_pages(old,fresh,equal);if(!status&&equal)*result=fscmprNoChange;return status;}catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsTransferDisplayInfoSubtrack(void* context,void* previous,void* current) {
    auto old=lookup_owned(tracks,context,previous),fresh=lookup_owned(tracks,context,current);if(!old||!fresh||!old->subtrack||!fresh->subtrack)return PTS_tserrInvalidQuery;
    try{return transfer_tracks(old,fresh);}catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsTransferDisplayInfoSubpage(void* context,void* previous,void* current) {
    auto old=lookup_owned(pages,context,previous),fresh=lookup_owned(pages,context,current);if(!old||!fresh||!old->subpage||!fresh->subpage)return PTS_tserrInvalidQuery;
    try{return transfer_pages(old,fresh);}catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
}
extern "C" __declspec(dllexport) int WINAPI FsUpdateFinitePage(void* context_handle,void* handle,void* break_input,void* section,FSFMTR* formatted,void** break_output) {
    if(!formatted||!break_output)return PTS_tserrNullOutputParameter;
    *formatted={};*break_output=nullptr;
    auto page=lookup_owned(pages,context_handle,handle);
    if(!page||page->subpage||page->bottomless)return PTS_tserrInvalidQuery;
    std::shared_ptr<BreakData> input;
    if(break_input){input=lookup_owned(records,context_handle,break_input);if(!input||!input->page)return PTS_tserrInvalidBreakRecord;section=input->section;}
    try {
        // Format and register a complete replacement before touching the live
        // page. Input positions are immutable and remain reusable after failure.
        std::shared_ptr<PageData> replacement;
        int status=build_page(page->context,section,false,replacement,input?input->position:nullptr);if(status)return status;
        if(replacement->stop==fmtrNoProgressOutOfSpace)return PTS_tserrNotImplemented;
        auto outgoing=replacement->continuation?make_record(page->context,replacement->continuation,true,section):nullptr;
        try {register_page(replacement);}catch(...){if(outgoing)destroy_record(context_handle,outgoing.get(),true);throw;}
        // Keep every old owner alive until after the registry lock is released.
        // Paragraph and break destruction reenters original managed callbacks.
        auto previous_track=page->track;auto previous_section=page->section;auto previous_position=page->continuation;
        bool live=false;
        {std::lock_guard<std::mutex> lock(gate);
            auto current=pages.find(handle);live=current!=pages.end()&&current->second==page;
            if(live){
                if(previous_section){for(const auto& track:previous_section->columns)tracks.erase(track.get());sections.erase(previous_section.get());}
                else tracks.erase(previous_track.get());
                pages.erase(replacement.get());
                page->track=replacement->track;page->section=replacement->section;page->details=replacement->details;
                page->continuation=replacement->continuation;page->stop=replacement->stop;page->forced_progress=replacement->forced_progress;
            }
        }
        if(!live){unregister_page(replacement);if(outgoing)destroy_record(context_handle,outgoing.get(),true);return PTS_tserrInvalidQuery;}
        formatted->kstop=page->stop;formatted->fForcedProgress=page->forced_progress;
        *break_output=outgoing?outgoing.get():nullptr;return 0;
    }catch(const std::bad_alloc&){return PTS_tserrOutOfMemory;}
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
extern "C" __declspec(dllexport) int WINAPI FsQueryFigureObjectDetails(void* context,void* handle,FSFIGUREDETAILS* output) {
    if(!output)return PTS_tserrNullOutputParameter;*output={};std::shared_ptr<ParaData> para;
    {std::lock_guard<std::mutex> lock(gate);if(contexts.find(context)==contexts.end())return PTS_tserrInvalidQuery;auto at=figures.find(handle);if(at!=figures.end())para=at->second.lock();}
    if(!para||para->context.get()!=context||!para->figure)return PTS_tserrInvalidQuery;
    *output=para->figure->details;return 0;
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
namespace {
template<class T> std::shared_ptr<T> lookup_weak_owned(std::unordered_map<void*,std::weak_ptr<T>>& map,void* context,void* handle) {
    std::shared_ptr<T> value;
    {std::lock_guard<std::mutex> lock(gate);if(contexts.find(context)==contexts.end())return {};auto found=map.find(handle);if(found==map.end())return {};value=found->second.lock();if(!value)map.erase(found);}
    return value&&value->context.get()==context?value:nullptr;
}
}
extern "C" __declspec(dllexport) int WINAPI FsQueryTableObjDetails(void* context,void* handle,FSTABLEOBJDETAILS* output) {
    if(!output)return PTS_tserrNullOutputParameter;*output={};auto data=lookup_weak_owned(tables,context,handle);if(!data)return PTS_tserrInvalidQuery;*output=data->details;return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQueryTableObjTableProperDetails(void* context,void* handle,FSTABLEDETAILS* output) {
    if(!output)return PTS_tserrNullOutputParameter;*output={};auto data=lookup_weak_owned(tables,context,handle);if(!data)return PTS_tserrInvalidQuery;*output=data->proper;return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQueryTableObjRowList(void* context,void* handle,int capacity,FSTABLEROWDESCRIPTION* output,int* count) {
    if(!count)return PTS_tserrNullOutputParameter;*count=0;auto data=lookup_weak_owned(tables,context,handle);if(!data)return PTS_tserrInvalidQuery;
    if(capacity<0||size_t(capacity)<data->rows.size()||(!output&&!data->rows.empty()))return PTS_tserrInvalidParameter;
    for(const auto& row:data->rows)output[(*count)++]=row->description;return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQueryTableObjRowDetails(void* context,void* handle,FSTABLEROWDETAILS* output) {
    if(!output)return PTS_tserrNullOutputParameter;*output={};auto data=lookup_weak_owned(table_rows,context,handle);if(!data)return PTS_tserrInvalidQuery;*output=data->details;return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQueryTableObjCellList(void* context,void* handle,int capacity,FSKUPDATE* updates,void** handles,FSTABLEKCELLMERGE* merges,int* count) {
    if(!count)return PTS_tserrNullOutputParameter;*count=0;auto data=lookup_weak_owned(table_rows,context,handle);if(!data)return PTS_tserrInvalidQuery;
    if(capacity<0||size_t(capacity)<data->cells.size()||((!updates||!handles||!merges)&&!data->cells.empty()))return PTS_tserrInvalidParameter;
    for(const auto& cell:data->cells){updates[*count]=cell?cell->update:fskupdNoChange;handles[*count]=cell?cell->handle:nullptr;merges[*count]=data->merges.empty()?fskcellmergeNo:data->merges[*count];++*count;}return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQueryTextDetails(void* context,void* handle,FSTEXTDETAILS* output) {
    if(!output)return PTS_tserrNullOutputParameter;*output={};auto text=find_text(context,handle);if(!text)return PTS_tserrInvalidQuery;
    output->fsktd=fsktdFull;auto& value=output->u.full;value.fswdir=text->direction;value.fsklines=fsklinesNormal;value.cLines=int(text->lines.size());
    value.cAttachedObjects=int(text->attached.size());
    if(!text->lines.empty()){value.dcpFirst=text->lines.front().dcpFirst;value.dcpLim=text->lines.back().dcpLim;}return 0;
}
extern "C" __declspec(dllexport) int WINAPI FsQueryAttachedObjectList(void* context,void* handle,int capacity,FSATTACHEDOBJECTDESCRIPTION* output,int* count) {
    if(!count)return PTS_tserrNullOutputParameter;*count=0;auto text=find_text(context,handle);if(!text)return PTS_tserrInvalidQuery;
    if(capacity<0||size_t(capacity)<text->attached.size()||(!output&&!text->attached.empty()))return PTS_tserrInvalidParameter;
    for(const auto& object:text->attached){const auto& para=object.para;auto& desc=output[(*count)++];desc={};desc.fsupdinf=para->desc.fsupdinf;desc.pfspara=para->desc.pfspara;desc.pfsparaclient=para->desc.pfsparaclient;desc.nmp=para->desc.nmp;desc.idobj=para->desc.idobj;desc.vrStart=attachment_rect(para).v;desc.dvrUsed=para->desc.dvrUsed;desc.fsbbox=para->desc.fsbbox;desc.dvrTopSpace=para->desc.dvrTopSpace;}return 0;
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
    output->fFootnotesAsPagenotes=1;auto& body=output->u.withpagenotes;body.fswdir=section->direction;body.fsrcSectionBody=section->description.fsrc;body.fsbboxSectionBody=section->description.fsbbox;body.cBasicColumns=int(section->columns.size());return 0;
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
    if(!segment||(direction!=PTS_fswdirES&&direction!=PTS_fswdirWS)||width<=0||height<0||columns<1||!info||rect.u<0||rect.v<0||rect.du<=0||rect.dv<0||
       int64_t(rect.u)+rect.du>width||(!bottomless&&int64_t(rect.v)+rect.dv>height))return PTS_tserrInvalidGeometry;
    if(bottomless&&columns!=1)return PTS_tserrNotImplemented;
    int64_t u=rect.u;
    for(int index=0;index<columns;++index){if(info[index].durBefore<0||info[index].durWidth<=0)return PTS_tserrInvalidGeometry;u+=int64_t(info[index].durBefore)+info[index].durWidth;if(u>int64_t(rect.u)+rect.du)return PTS_tserrInvalidGeometry;}
    FigureGeometry subpage_geometry({0,0,width,height},rect);
    auto page=std::make_shared<PageData>(context);page->subpage=true;page->bottomless=bottomless;
    if(columns>1){page->section=std::make_shared<SectionData>(context);page->section->direction=direction;page->section->description.nms=segment;page->section->description.fsrc=rect;}
    auto position=input;bool finished=false,stop_page=false;u=rect.u;
    for(int index=0;index<columns;++index) {
        u+=info[index].durBefore;FSRECT column{int(u),rect.v,info[index].durWidth,bottomless?INT32_MAX-rect.v:rect.dv};u+=column.du;
        std::shared_ptr<TrackData> track;MarginOwner margin{context};
        if(finished||stop_page){track=std::make_shared<TrackData>(context);track->segment=segment;track->direction=direction;track->rect=column;}
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
    if(page->section)for(const auto& track:page->section->columns)clear_track(track,page->inherit_updates);
    else {clear_track(page->track,page->inherit_updates);page->subpage_details.u.simple.trackdescr.fsupdinf.fskupd=page->inherit_updates?fskupdInherited:fskupdNoChange;}
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
    if(footnote||segment_areas||height_areas||overhang)return PTS_tserrNotImplemented;
    std::shared_ptr<BreakData> input;
    if(break_input){input=lookup_owned(records,context_handle,break_input);if(!input||input->page||!input->subpage)return PTS_tserrInvalidBreakRecord;}
    try {
        std::shared_ptr<PageData> page;MarginOwner margin{context};
        int status=build_subpage(context,segment,direction,*rect,width,height,columns,info,false,empty_ok,suppress,previous,suppress_break,input?input->position:nullptr,page,&margin.value,*used,*bbox);if(status){*used=0;*bbox={};return status;}
        if(balance){status=balance_columns(page,rect->dv,margin,[&](int limit,std::shared_ptr<PageData>& trial,void** next_margin){FSRECT area=*rect;area.dv=limit;int trial_used=0;FSBBOX trial_box{};return build_subpage(context,segment,direction,area,width,height,columns,info,false,empty_ok,suppress,previous,suppress_break,input?input->position:nullptr,trial,next_margin,trial_used,trial_box);});if(status){*used=0;*bbox={};return status;}
            *used=0;if(page->section){for(const auto& track:page->section->columns)*used=std::max(*used,track->used);*bbox=page->subpage_details.u.complex.fsbbox;}else {*used=page->track->used;*bbox=page->track->bbox;}}
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
