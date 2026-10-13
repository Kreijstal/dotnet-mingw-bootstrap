// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#include "ParagraphServicesNative.h"
static int creates=0,destroys=0,fail_id=-1;
static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static int WINAPI create_object(void* client,void* context,FSCBKOBJ* callbacks,uint32_t flags,int id,void** output) {
    if(client!=reinterpret_cast<void*>(55)||!context||!callbacks||flags!=0x140||id<0||id>1)return PTS_tserrInvalidParameter;
    ++creates;*output=nullptr;if(id==fail_id)return PTS_tserrCallbackException;
    *output=reinterpret_cast<void*>(100+id);return 0;
}
static int WINAPI destroy_object(void* value){if(value!=reinterpret_cast<void*>(100)&&value!=reinterpret_cast<void*>(101))return PTS_tserrInvalidParameter;++destroys;return 0;}
int main(int argc,char** argv) {
    try {
        require(argc==2,"usage: check DLL");auto dll=LoadLibraryA(argv[1]);require(dll!=nullptr,"native DLL load");
        using Install=int(WINAPI*)(FSIMETHODS*,FSIMETHODS*,void**,int*);using Create=int(WINAPI*)(FSCONTEXTINFO*,void**);using Destroy=int(WINAPI*)(void*);using State=int(WINAPI*)(void*,FSCONTEXTINFO*,FSIMETHODS*,void**);
#define PROC(name,type) auto name=reinterpret_cast<type>(GetProcAddress(dll,#name));require(name!=nullptr,#name)
        PROC(CreateInstalledObjectsInfo,Install);PROC(DestroyInstalledObjectsInfo,Destroy);PROC(CreateDocContext,Create);PROC(DestroyDocContext,Destroy);PROC(WpfGetParagraphContextState,State);
        FSIMETHODS methods{};methods.pfnCreateContext=reinterpret_cast<void*>(create_object);methods.pfnDestroyContext=reinterpret_cast<void*>(destroy_object);methods.pfnFormatParaFinite=reinterpret_cast<void*>(1234);
        void* installed=nullptr;int count=0;
        require(CreateInstalledObjectsInfo(&methods,&methods,&installed,&count)==0&&installed&&count==4,"source installed object registration");
        FSCONTEXTINFO info{};info.fsffi=0x140;info.drMinColumnBalancingStep=3000;info.cInstalledObjects=count;info.pInstalledObjects=installed;info.pfsclient=reinterpret_cast<void*>(55);
        methods.pfnCreateContext=nullptr;methods.pfnFormatParaFinite=nullptr; // caller mutations must not alter retained tables
        void* context=nullptr;require(CreateDocContext(&info,&context)==0&&context&&creates==2,"copied object callbacks create both owned contexts");
        FSCONTEXTINFO actual{};FSIMETHODS copied[2]{};void* contexts[2]{};
        require(WpfGetParagraphContextState(context,&actual,copied,contexts)==0&&actual.fsffi==0x140&&actual.pfsclient==reinterpret_cast<void*>(55)&&copied[0].pfnFormatParaFinite==reinterpret_cast<void*>(1234)&&contexts[0]==reinterpret_cast<void*>(100)&&contexts[1]==reinterpret_cast<void*>(101),"retained context ABI/configuration/object identity snapshot");
        require(DestroyInstalledObjectsInfo(installed)==0&&DestroyInstalledObjectsInfo(installed)==PTS_tserrInvalidParameter,"installed registration rejects double destruction");
        require(WpfGetParagraphContextState(context,&actual,copied,contexts)==0,"document independently retains installed callback ownership");
        require(DestroyDocContext(context)==0&&destroys==2&&DestroyDocContext(context)==PTS_tserrInvalidLsContext,"document teardown calls source destructors once");
        require(WpfGetParagraphContextState(context,&actual,copied,contexts)==PTS_tserrInvalidLsContext&&!contexts[0]&&!actual.pfsclient,"destroyed state queries are empty and reject without dereferencing");
        context=reinterpret_cast<void*>(1);require(CreateDocContext(&info,&context)==PTS_tserrInvalidParameter&&!context,"destroyed installed registration cannot create a new document");
        methods.pfnCreateContext=reinterpret_cast<void*>(create_object);require(CreateInstalledObjectsInfo(&methods,&methods,&installed,&count)==0,"rollback registration");info.pInstalledObjects=installed;
        fail_id=1;creates=destroys=0;context=reinterpret_cast<void*>(1);
        require(CreateDocContext(&info,&context)==PTS_tserrCallbackException&&!context&&creates==2&&destroys==1,"second object creation failure rolls back the first object and clears output");
        fail_id=-1;info.version=1;require(CreateDocContext(&info,&context)==PTS_tserrInvalidParameter&&!context,"unsupported version rejects without fabricated context");
        require(DestroyInstalledObjectsInfo(installed)==0,"rollback registration disposal");
        std::printf("native paragraph records: context=%zu, methods=%zu, callbacks=%zu; copied ownership, callbacks, rollback, invalid states and teardown PASS\n",sizeof(FSCONTEXTINFO),sizeof(FSIMETHODS),sizeof(FSCBK));return 0;
    }catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}
}
