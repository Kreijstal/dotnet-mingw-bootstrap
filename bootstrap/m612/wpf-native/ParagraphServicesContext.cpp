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
