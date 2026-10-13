// SPDX-License-Identifier: MIT
// Owned source paragraph-service registration/context state. Page layout is separate.
#include <windows.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <new>
#include "ParagraphServicesNative.h"
namespace {
struct InstalledObjects { FSIMETHODS methods[2]; };
struct Context {
    FSCONTEXTINFO info{};
    std::shared_ptr<InstalledObjects> installed;
    void* object_contexts[2]{};
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
        if(found==contexts.end())return PTS_tserrInvalidLsContext;context=found->second;contexts.erase(found);}
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
