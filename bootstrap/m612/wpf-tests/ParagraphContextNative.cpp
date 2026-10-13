// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#include <algorithm>
#include <climits>
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
        using Transform=int(WINAPI*)(uint32_t,const FSRECT*,const FSRECT*,uint32_t,FSRECT*);
        using TransformBox=int(WINAPI*)(uint32_t,const FSRECT*,const FSBBOX*,uint32_t,FSBBOX*);
        PROC(FsTransformRectangle,Transform);PROC(FsTransformBbox,TransformBox);
        auto equal=[](FSRECT a,FSRECT b){return a.u==b.u&&a.v==b.v&&a.du==b.du&&a.dv==b.dv;};
        FSRECT page{17,29,900,600},physical{72,103,83,47};
        // Independent corner projection: construct the axes and origin named
        // by ES/EN/SE/SW/WS/WN/NE/NW, then project physical corner points.
        const int ux[]={1,1,0,0,-1,-1,0,0},uy[]={0,0,1,1,0,0,-1,-1};
        const int vx[]={0,0,1,-1,0,0,1,-1},vy[]={1,-1,0,0,1,-1,0,0};
        auto project=[&](int direction,FSRECT r) {
            int originX=(ux[direction]<0||vx[direction]<0)?page.du:0;
            int originY=(uy[direction]<0||vy[direction]<0)?page.dv:0;
            int loU=INT_MAX,loV=INT_MAX,hiU=INT_MIN,hiV=INT_MIN;
            for(int dx:{0,r.du})for(int dy:{0,r.dv}) {
                int x=r.u+dx-page.u-originX,y=r.v+dy-page.v-originY;
                int u=x*ux[direction]+y*uy[direction],v=x*vx[direction]+y*vy[direction];
                loU=std::min(loU,u);hiU=std::max(hiU,u);loV=std::min(loV,v);hiV=std::max(hiV,v);
            }
            return FSRECT{page.u+loU,page.v+loV,hiU-loU,hiV-loV};
        };
        for(uint32_t in=0;in<8;++in)for(uint32_t out=0;out<8;++out) {
            FSRECT src=project(in,physical),expected=project(out,physical),actual{};
            require(FsTransformRectangle(in,&page,&src,out,&actual)==0&&equal(actual,expected),"all writing directions agree with independent physical corner projection");
            require(FsTransformRectangle(in,&page,&src,out,&src)==0&&equal(src,expected),"rectangle ref/out aliasing");
            FSBBOX box{1,project(in,physical)};
            require(FsTransformBbox(in,&page,&box,out,&box)==0&&box.fDefined==1&&equal(box.fsrc,expected),"defined bbox geometry and aliasing");
        }
        FSRECT outside{-100,-50,0,20},mirrored{};
        require(FsTransformRectangle(0,&page,&outside,4,&mirrored)==0&&mirrored.u==1034&&mirrored.v==-50&&mirrored.du==0,"outside-page and zero-width rectangles are retained");
        FSBBOX undefined{0,{INT_MIN,INT_MAX,-1,-1}},empty{};
        require(FsTransformBbox(0,&page,&undefined,4,&empty)==0&&!empty.fDefined&&equal(empty.fsrc,FSRECT{}),"undefined bbox does not inspect meaningless coordinates");
        FSRECT overflow{INT_MIN,0,1,1},invalid{1,2,3,4};
        require(FsTransformRectangle(0,&page,&overflow,4,&invalid)==PTS_tserrInvalidParameter&&equal(invalid,FSRECT{}),"overflow rejects without narrowing or partial output");
        require(FsTransformRectangle(8,&page,&physical,0,&invalid)==PTS_tserrInvalidParameter&&equal(invalid,FSRECT{}),"invalid direction rejects and clears output");
        require(FsTransformRectangle(0,nullptr,&physical,0,&invalid)==PTS_tserrInvalidParameter&&equal(invalid,FSRECT{}),"null geometry rejects");
        require(FsTransformRectangle(0,&page,&physical,0,nullptr)==PTS_tserrNullOutputParameter,"null output rejects");
        FSRECT negative{0,0,-1,1};require(FsTransformRectangle(0,&page,&negative,0,&invalid)==PTS_tserrInvalidParameter,"negative extent rejects");
        std::puts("paragraph geometry: 64 direction pairs, corner projection, aliasing, empty/outside, overflow and invalid arguments PASS");
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
