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
static int lines_created=0,lines_destroyed=0,breaks_destroyed=0,clients_created=0,clients_destroyed=0,fail_line=-1,columns=1,available_height=80,invalid_columns=false,hard_break=0,keep_mode=0,keep_next=0,para_count=2;
static bool containers=false,lose_subtrack=false;
static void* page_context=nullptr;
using SubtrackFormat=int(WINAPI*)(void*,void*,int,void*,int,void*,void*,int,int,uint32_t,const FSRECT*,void*,FSKCLEAR,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA,FSFMTR*,void**,void**,int*,FSBBOX*,void**,FSKCLEAR*,int*);
static SubtrackFormat format_subtrack=nullptr;
static int(WINAPI* destroy_subtrack)(void*,void*)=nullptr;
static int(WINAPI* destroy_subtrack_record)(void*,void*)=nullptr;
static int WINAPI container_format(void*,void*,void* input,int previous,void* name,int area,void* footnote,void* geometry,int empty,int suppress,uint32_t direction,FSRECT* rect,void* margin,FSKCLEAR clear,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA suppress_break,int,
    FSFMTR* formatted,void** output,void** br,int* used,FSBBOX* bbox,void** margin_out,FSKCLEAR* clear_out,int* top,int* can_break) {
    *can_break=0;
    int status=format_subtrack(page_context,input,previous,name,area,footnote,geometry,empty,suppress,direction,rect,margin,clear,suppress_break,formatted,output,br,used,bbox,margin_out,clear_out,top);
    if(!status&&lose_subtrack){*output=*br=nullptr;return PTS_tserrCallbackException;}
    return status;
}
static int WINAPI container_destroy_record(void*,void* record){return destroy_subtrack_record(page_context,record);}
static int WINAPI container_destroy(void* track){return destroy_subtrack(page_context,track);}
static int WINAPI page_geometry(void*,void*,uint32_t* direction,int* header,int* width,int* height,FSRECT* margin) {
    *direction=0;*header=0;*width=columns==2?240:200;*height=140;*margin={10,20,columns==2?210:100,available_height};return 0;
}
static int WINAPI section_properties(void*,void*,int* page,uint32_t* direction,int* balance,int* cols,int* segments,int* heights) {
    *page=0;*direction=0;*balance=0;*cols=columns;*segments=*heights=0;return 0;
}
static int WINAPI column_info(void*,void*,uint32_t,int capacity,FSCOLUMNINFO* output,int* actual) {
    if(capacity!=2)return PTS_tserrInvalidParameter;output[0]={0,100};output[1]={10,invalid_columns?250:100};*actual=2;return 0;
}
static int WINAPI main_segment(void*,void*,void** segment){*segment=reinterpret_cast<void*>(77);return 0;}
static int WINAPI first_para(void*,void* segment,int* found,void** name){if(segment!=reinterpret_cast<void*>(77)&&!containers)return PTS_tserrInvalidParameter;*found=1;*name=segment==reinterpret_cast<void*>(77)?reinterpret_cast<void*>(101):reinterpret_cast<void*>(201);return 0;}
static int WINAPI next_para(void*,void* segment,void* current,int* found,void** next){*found=segment==reinterpret_cast<void*>(77)&&reinterpret_cast<uintptr_t>(current)<uintptr_t(100+para_count);*next=*found?reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(current)+1):nullptr;return 0;}
static int WINAPI para_properties(void*,void* name,FSPAP* pap){*pap={};pap->idobj=containers&&reinterpret_cast<uintptr_t>(name)<200?0:-1;pap->fKeepWithNext=keep_next&&name==reinterpret_cast<void*>(102);if(name==reinterpret_cast<void*>(102)){pap->fBreakPageBefore=hard_break==1;pap->fBreakColumnBefore=hard_break==2;}return 0;}
static int WINAPI para_client(void*,void* name,void** output){++clients_created;*output=name;return 0;}
static int WINAPI destroy_para_client(void*,void*){++clients_destroyed;return 0;}
static int WINAPI text_properties(void*,void* name,int,FSTXTPROPS* props){*props={};props->fKeepTogether=keep_mode==2||(keep_mode==1&&name==reinterpret_cast<void*>(102));return 0;}
static int WINAPI line_format(void*,void*,void*,int,int dcp,void* incoming,uint32_t direction,
    int start,int width,int track_start,int track_width,int margin,int hyphen,int left,int right,int first,int last,int suppress,
    void** line,int* consumed,void** br,int* forced,FSFLRES* result,int* ascent,int* descent,int* bbox,int* bbox_width,int* depend,int* reformat) {
    if(direction||start!=track_start||width!=100||(track_start!=10&&track_start!=120)||track_width!=100||margin!=track_start||hyphen||left||right||last||first!=(dcp==0)||bool(incoming)!=(dcp!=0))return PTS_tserrInvalidParameter;
    ++lines_created;*line=reinterpret_cast<void*>(1000+lines_created);*br=reinterpret_cast<void*>(2000+lines_created);
    *consumed=5;*forced=0;*result=dcp==0?fsflrOutOfSpace:fsflrEndOfParagraph;*ascent=7;*descent=3;*bbox=start;*bbox_width=75;*depend=*reformat=0;
    if(lines_created==fail_line)return PTS_tserrCallbackException;
    return 0;
}
static int WINAPI destroy_line(void*,void*){++lines_destroyed;return 0;}
static int WINAPI destroy_break(void*,void*){++breaks_destroyed;return 0;}
static int WINAPI destroy_margin(void*,void*){return 0;}
static void reset_page_counts(){lines_created=lines_destroyed=breaks_destroyed=clients_created=clients_destroyed=0;}

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
        using PageCreate=int(WINAPI*)(void*,void*,void*,FSFMTR*,void**,void**);
        using QueryPage=int(WINAPI*)(void*,void*,FSPAGEDETAILS*);
        using QueryTrack=int(WINAPI*)(void*,void*,FSTRACKDETAILS*);
        using QueryParas=int(WINAPI*)(void*,void*,int,FSPARADESCRIPTION*,int*);
        using QueryText=int(WINAPI*)(void*,void*,FSTEXTDETAILS*);
        using QueryLines=int(WINAPI*)(void*,void*,int,FSLINEDESCRIPTIONSINGLE*,int*);
        using PageAction=int(WINAPI*)(void*,void*);
        PROC(FsCreatePageFinite,PageCreate);PROC(FsDestroyPage,PageAction);PROC(FsDestroySubtrack,PageAction);
        PROC(FsQueryPageDetails,QueryPage);PROC(FsQueryTrackDetails,QueryTrack);PROC(FsQueryTrackParaList,QueryParas);
        PROC(FsQueryTextDetails,QueryText);PROC(FsQueryLineListSingle,QueryLines);PROC(FsClearUpdateInfoInPage,PageAction);
        format_subtrack=reinterpret_cast<SubtrackFormat>(GetProcAddress(dll,"FsFormatSubtrackFinite"));destroy_subtrack=FsDestroySubtrack;
        require(format_subtrack!=nullptr,"finite subtrack export");
        destroy_subtrack_record=reinterpret_cast<decltype(destroy_subtrack_record)>(GetProcAddress(dll,"FsDestroySubtrackBreakRecord"));require(destroy_subtrack_record!=nullptr,"subtrack record destruction export");
        methods.pfnDestroyBreakRecord=reinterpret_cast<void*>(container_destroy_record);
        methods.pfnFormatParaFinite=reinterpret_cast<void*>(container_format);methods.pfnDestroyPara=reinterpret_cast<void*>(container_destroy);
        require(CreateInstalledObjectsInfo(&methods,&methods,&installed,&count)==0,"page installed registration");
        info.version=0;info.pInstalledObjects=installed;
        auto& gen=info.fscbk.cbkgen;auto& txt=info.fscbk.cbktxt;
        gen.pfnGetPageDimensions=reinterpret_cast<void*>(page_geometry);gen.pfnGetSectionProperties=reinterpret_cast<void*>(section_properties);gen.pfnGetMainTextSegment=reinterpret_cast<void*>(main_segment);gen.pfnGetSectionColumnInfo=reinterpret_cast<void*>(column_info);
        gen.pfnGetFirstPara=reinterpret_cast<void*>(first_para);gen.pfnGetNextPara=reinterpret_cast<void*>(next_para);gen.pfnGetParaProperties=reinterpret_cast<void*>(para_properties);
        gen.pfnCreateParaclient=reinterpret_cast<void*>(para_client);gen.pfnDestroyParaclient=reinterpret_cast<void*>(destroy_para_client);
        txt.pfnGetTextProperties=reinterpret_cast<void*>(text_properties);txt.pfnFormatLine=reinterpret_cast<void*>(line_format);
        txt.pfnDestroyLine=reinterpret_cast<void*>(destroy_line);txt.pfnDestroyLineBreakRecord=reinterpret_cast<void*>(destroy_break);
        info.fscbk.cbkobj.pfnDestroyMcsclient=reinterpret_cast<void*>(destroy_margin);
        require(CreateDocContext(&info,&context)==0,"page context creation");page_context=context;reset_page_counts();
        FSFMTR formatted{};void* page_handle=nullptr;void* page_break=nullptr;
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_handle&&!page_break&&formatted.kstop==fmtrGoalReached,"finite page creates actual callback paragraphs and lines");
        require(lines_created==4&&clients_created==2&&!lines_destroyed&&!breaks_destroyed&&!clients_destroyed,"page retains line/client/break handles until release");
        require(DestroyDocContext(context)==PTS_tserrInvalidPtsContext,"context cannot be destroyed with live pages");
        FSPAGEDETAILS details{};require(FsQueryPageDetails(context,page_handle,&details)==0&&details.fSimple&&details.fskupd==fskupdNew&&details.u.simple.trackdescr.fsrc.u==10&&details.u.simple.trackdescr.fsrc.v==20,"original callback page margins and update state");
        void* track=details.u.simple.trackdescr.pfstrack;FSTRACKDETAILS track_details{};
        require(FsQueryTrackDetails(context,track,&track_details)==0&&track_details.cParas==2,"track actual paragraph count");
        FSPARADESCRIPTION paras[2]{};int actual_count=0;
        require(FsQueryTrackParaList(context,track,2,paras,&actual_count)==0&&actual_count==2&&paras[0].dvrUsed==20&&paras[1].dvrUsed==20&&paras[0].fsbbox.fsrc.u==10&&paras[1].fsbbox.fsrc.v==40,"paragraph retained geometry and identities");
        require(FsQueryTrackParaList(context,track,1,paras,&actual_count)==PTS_tserrInvalidParameter&&!actual_count,"short paragraph array rejects without partial writes");
        FSTEXTDETAILS text_details{};
        require(FsQueryTextDetails(context,paras[0].pfspara,&text_details)==0&&text_details.fsktd==fsktdFull&&text_details.u.full.cLines==2&&text_details.u.full.dcpFirst==0&&text_details.u.full.dcpLim==10,"text details describe actual formatted source range");
        FSLINEDESCRIPTIONSINGLE lines[2]{};
        require(FsQueryLineListSingle(context,paras[0].pfspara,2,lines,&actual_count)==0&&actual_count==2&&lines[0].dcpFirst==0&&lines[0].dcpLim==5&&lines[1].dcpFirst==5&&lines[1].dcpLim==10&&lines[0].urStart==10&&lines[0].vrStart==20&&lines[1].vrStart==30&&lines[1].pfsbreakreclineclient==reinterpret_cast<void*>(2001),"line metrics, ranges and continuation record");
        require(FsQueryPageDetails(reinterpret_cast<void*>(1),page_handle,&details)==PTS_tserrInvalidQuery&&!details.fSimple,"foreign context cannot query a page");
        require(FsDestroySubtrack(context,track)==PTS_tserrInvalidQuery,"page-owned root track cannot be destroyed as a subtrack");
        require(FsClearUpdateInfoInPage(context,page_handle)==0&&FsQueryPageDetails(context,page_handle,&details)==0&&details.fskupd==fskupdNoChange,"page update clear is retained");
        require(FsDestroyPage(context,page_handle)==0&&lines_destroyed==4&&breaks_destroyed==4&&clients_destroyed==2&&FsDestroyPage(context,page_handle)==PTS_tserrInvalidQuery,"page teardown releases every callback handle exactly once");
        require(FsQueryTextDetails(context,paras[0].pfspara,&text_details)==PTS_tserrInvalidQuery&&FsQueryTrackDetails(context,track,&track_details)==PTS_tserrInvalidQuery,"page teardown invalidates child queries");
        containers=true;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_handle&&clients_created==4&&lines_created==4,"container callbacks recursively format real subtracks");
        require(FsDestroyPage(context,page_handle)==0&&clients_destroyed==4&&lines_destroyed==4&&breaks_destroyed==4,"recursive container teardown releases child tracks and callback handles");
        reset_page_counts();lose_subtrack=true;
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"object callback failure after successful subtrack export rolls back a reset out handle");
        available_height=10;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&!page_break&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"object callback failure resets both partial subtrack and continuation outputs without leaking either");
        available_height=80;containers=false;lose_subtrack=false;
        reset_page_counts();fail_line=2;page_handle=reinterpret_cast<void*>(1);page_break=reinterpret_cast<void*>(1);
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&!page_break&&lines_created==lines_destroyed&&lines_created==breaks_destroyed&&clients_created==clients_destroyed,"partial callback allocation failure rolls back line/client/break ownership");
        fail_line=-1;available_height=10;reset_page_counts();
        PROC(FsDestroyPageBreakRecord,PageAction);
        void* incoming_record=nullptr;keep_mode=2;
        for(int number=0;number<4;++number) {
            require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_handle,"finite continuation formats the next real page");
            if(incoming_record)require(FsDestroyPageBreakRecord(context,incoming_record)==0,"input record can be released after continuation");
            require(FsQueryPageDetails(context,page_handle,&details)==0,"paginated page details");
            require(FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1,"one accepted text line/paragraph per short page");
            require(paras[0].nmp==reinterpret_cast<void*>(101+number/2)&&FsQueryTextDetails(context,paras[0].pfspara,&text_details)==0&&text_details.u.full.dcpFirst==(number%2)*5&&text_details.u.full.dcpLim==(number%2)*5+5,"four pages cover both source paragraphs exactly once without gaps/overlap");
            require((number<3)==bool(page_break)&&formatted.kstop==(number<3?fmtrBrokenOutOfSpace:fmtrGoalReached),"page result and continuation identity agree with source exhaustion");
            require(FsDestroyPage(context,page_handle)==0,"page can be disposed while its independent continuation remains");incoming_record=page_break;
        }
        require(!incoming_record&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"pagination releases speculative and accepted lines and independent records exactly once");
        keep_mode=0;
        std::puts("native finite pagination: four-page exact source coverage, real break records, input/page-independent lifetime and complete callback cleanup PASS");
        available_height=30;keep_mode=1;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_break,"keep-together paragraph is deferred rather than partially consumed");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].nmp==reinterpret_cast<void*>(101),"first page retains preceding text and no fragment of the kept paragraph");
        require(FsDestroyPage(context,page_handle)==0,"keep-together first page disposal");incoming_record=page_break;
        require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break&&FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].nmp==reinterpret_cast<void*>(102)&&FsQueryTextDetails(context,paras[0].pfspara,&text_details)==0&&text_details.u.full.dcpFirst==0&&text_details.u.full.dcpLim==10,"kept paragraph resumes intact from its first character on the next page");
        require(FsDestroyPageBreakRecord(context,incoming_record)==0&&FsDestroyPage(context,page_handle)==0&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"kept and speculative line/break/client resources are released exactly once");
        keep_mode=0;available_height=10;
        std::puts("native keep-together: intact deferral/resume, preceding source preservation, oversized kept paragraph progress and exact teardown PASS");
        para_count=3;keep_next=1;available_height=40;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_break,"keep-with-next chain rolls back an already accepted heading when following text cannot fit");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].nmp==reinterpret_cast<void*>(101),"heading is removed from the preceding page without dropping prefix content");
        require(FsDestroyPage(context,page_handle)==0,"keep-chain first page release");incoming_record=page_break;
        require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break&&FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==2&&paras[0].nmp==reinterpret_cast<void*>(102)&&paras[1].nmp==reinterpret_cast<void*>(103),"heading and following paragraph resume together from chain start");
        require(FsDestroyPageBreakRecord(context,incoming_record)==0&&FsDestroyPage(context,page_handle)==0&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"accepted, rolled-back and resumed keep-chain handles are released exactly once");
        keep_next=0;para_count=2;available_height=10;
        std::puts("native keep-with-next: accepted heading rollback, intact chain resume, preceding content preservation and exact callback cleanup PASS");
        using CloneRecord=int(WINAPI*)(void*,void*,void**);
        PROC(FsDuplicateSubtrackBreakRecord,CloneRecord);reset_page_counts();
        FSRECT small{10,20,100,10};void* subtrack=nullptr;void* subrecord=nullptr;void* clone=nullptr;void* margin=nullptr;int used=0,top_space=0;FSBBOX bbox{};FSKCLEAR clear{};
        require(format_subtrack(context,nullptr,0,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&small,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==0&&subrecord&&used==10,"standalone finite subtrack exports a continuation");
        require(FsDuplicateSubtrackBreakRecord(context,subrecord,&clone)==0&&clone&&clone!=subrecord,"subtrack clone has independent wrapper identity");
        require(destroy_subtrack_record(context,subrecord)==0&&destroy_subtrack(context,subtrack)==0,"original subtrack and record can be released before their clone");
        require(DestroyDocContext(context)==PTS_tserrInvalidPtsContext&&FsDestroyPageBreakRecord(context,clone)==PTS_tserrInvalidBreakRecord,"clone retains context and rejects the wrong record destruction kind");
        require(format_subtrack(context,clone,1,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&small,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==0&&used==10,"clone retains original line-break state and resumes after page/subtrack destruction");
        require(destroy_subtrack_record(context,clone)==0&&destroy_subtrack_record(context,clone)==PTS_tserrInvalidBreakRecord&&destroy_subtrack(context,subtrack)==0&&destroy_subtrack_record(context,subrecord)==0,"cloned and continued records tear down independently and reject double release");
        require(lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"subtrack cloning releases every callback handle exactly once");
        clone=reinterpret_cast<void*>(1);require(FsDuplicateSubtrackBreakRecord(context,reinterpret_cast<void*>(1),&clone)==PTS_tserrInvalidBreakRecord&&!clone,"invalid clone clears output without dereferencing");
        std::puts("native continuation cloning: independent identities, retained resume state/context, wrong/stale records, reset-output rollback and exact teardown PASS");
        available_height=20;columns=2;reset_page_counts();
        using QuerySections=int(WINAPI*)(void*,void*,int,FSSECTIONDESCRIPTION*,int*);
        using QuerySection=int(WINAPI*)(void*,void*,FSSECTIONDETAILS*);
        using QueryColumns=int(WINAPI*)(void*,void*,int,FSTRACKDESCRIPTION*,int*);
        PROC(FsQueryPageSectionList,QuerySections);PROC(FsQuerySectionDetails,QuerySection);PROC(FsQuerySectionBasicColumnList,QueryColumns);
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_handle&&!page_break&&formatted.kstop==fmtrGoalReached,"two-column page fills both original source tracks");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&!details.fSimple&&details.u.complex.cSections==1,"column page reports complex section hierarchy");
        FSSECTIONDESCRIPTION section_desc{};FSSECTIONDETAILS section_details{};FSTRACKDESCRIPTION column_desc[2]{};
        require(FsQueryPageSectionList(context,page_handle,1,&section_desc,&actual_count)==0&&actual_count==1&&FsQuerySectionDetails(context,section_desc.pfssection,&section_details)==0&&section_details.fFootnotesAsPagenotes&&section_details.u.withpagenotes.cBasicColumns==2,"section query reports owned actual columns");
        require(FsQuerySectionBasicColumnList(context,section_desc.pfssection,2,column_desc,&actual_count)==0&&actual_count==2&&column_desc[0].fsrc.u==10&&column_desc[1].fsrc.u==120&&column_desc[0].fsrc.du==100&&column_desc[1].fsrc.du==100,"callback column widths and gap define exact page coordinates");
        for(int col=0;col<2;++col) {
            require(FsQueryTrackParaList(context,column_desc[col].pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].nmp==reinterpret_cast<void*>(101+col),"column continuation covers the next paragraph without restarting");
            require(FsQueryLineListSingle(context,paras[0].pfspara,2,lines,&actual_count)==0&&actual_count==2&&lines[0].urStart==10+110*col&&lines[0].vrStart==20,"queried glyph lines occupy their own columns");
        }
        require(FsQuerySectionBasicColumnList(context,section_desc.pfssection,1,column_desc,&actual_count)==PTS_tserrInvalidParameter&&!actual_count,"short column query rejects without partial output");
        require(FsClearUpdateInfoInPage(context,page_handle)==0&&FsQuerySectionBasicColumnList(context,section_desc.pfssection,2,column_desc,&actual_count)==0&&column_desc[0].fsupdinf.fskupd==fskupdNoChange&&column_desc[1].fsupdinf.fskupd==fskupdNoChange,"update clearing propagates through every column");
        require(FsDestroyPage(context,page_handle)==0&&FsQuerySectionDetails(context,section_desc.pfssection,&section_details)==PTS_tserrInvalidQuery&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"page teardown invalidates section/columns and releases all callback resources");
        available_height=80;hard_break=1;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_break&&formatted.kstop==fmtrBrokenPageBreakBeforePara,"explicit page break before a paragraph stops the page despite spare column space");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryPageSectionList(context,page_handle,1,&section_desc,&actual_count)==0&&FsQuerySectionBasicColumnList(context,section_desc.pfssection,2,column_desc,&actual_count)==0,"page-break section/column query");
        require(FsQueryTrackDetails(context,column_desc[1].pfstrack,&track_details)==0&&track_details.cParas==0,"page break leaves remaining columns empty without consuming following text");
        require(FsDestroyPage(context,page_handle)==0,"explicit page-break page release");incoming_record=page_break;
        require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break&&formatted.kstop==fmtrGoalReached,"pending page break is suppressed exactly once at the destination page start");
        require(FsDestroyPageBreakRecord(context,incoming_record)==0&&FsDestroyPage(context,page_handle)==0&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"explicit page-break continuation cleanup");
        hard_break=2;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break&&formatted.kstop==fmtrGoalReached,"explicit column break resumes within the same page");
        require(FsQueryPageSectionList(context,page_handle,1,&section_desc,&actual_count)==0&&FsQuerySectionBasicColumnList(context,section_desc.pfssection,2,column_desc,&actual_count)==0&&FsQueryTrackParaList(context,column_desc[1].pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].nmp==reinterpret_cast<void*>(102),"column-break paragraph starts at the second column without duplication");
        require(FsDestroyPage(context,page_handle)==0&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"explicit column-break cleanup");hard_break=0;
        std::puts("native explicit breaks: page stop/skipped columns, column resume, destination suppression, exact character ownership and record cleanup PASS");
        invalid_columns=true;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrInvalidGeometry&&!page_handle&&!page_break&&!clients_created,"invalid column extents reject before client allocation");
        invalid_columns=false;fail_line=4;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&!page_break&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"later-column callback failure rolls back earlier-column ownership");
        fail_line=-1;available_height=80;
        std::puts("native column pages: exact source continuation/geometry, owned section/track hierarchy, capacity/update/stale queries, invalid bounds and later-column rollback PASS");
        columns=1;available_height=10;reset_page_counts();
        using BottomlessCreate=int(WINAPI*)(void*,void*,FSFMTRBL*,void**);
        using BottomlessUpdate=int(WINAPI*)(void*,void*,void*,FSFMTRBL*);
        PROC(FsCreatePageBottomless,BottomlessCreate);PROC(FsUpdateBottomlessPage,BottomlessUpdate);
        FSFMTRBL bottomless_result{};
        require(FsCreatePageBottomless(context,reinterpret_cast<void*>(66),&bottomless_result,&page_handle)==0&&bottomless_result==fmtrblGoalReached,"bottomless formatting consumes full text beyond finite available height");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&details.u.simple.trackdescr.fsrc.dv==40,"bottomless page height is actual consumed height");
        void* previous_track=details.u.simple.trackdescr.pfstrack;
        require(FsUpdateBottomlessPage(context,page_handle,reinterpret_cast<void*>(66),&bottomless_result)==0&&lines_created==8&&lines_destroyed==4&&clients_created==4&&clients_destroyed==2,"bottomless update retains page handle and releases superseded callback handles");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&details.u.simple.trackdescr.pfstrack!=previous_track&&FsQueryTrackDetails(context,previous_track,&track_details)==PTS_tserrInvalidQuery,"successful replacement invalidates old layout queries");
        previous_track=details.u.simple.trackdescr.pfstrack;fail_line=lines_created+2;
        require(FsUpdateBottomlessPage(context,page_handle,reinterpret_cast<void*>(66),&bottomless_result)==PTS_tserrCallbackException&&FsQueryPageDetails(context,page_handle,&details)==0&&details.u.simple.trackdescr.pfstrack==previous_track,"failed bottomless replacement preserves the previous valid page and layout");
        fail_line=-1;require(FsDestroyPage(context,page_handle)==0&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"bottomless update/failure/teardown releases every source callback resource");
        available_height=80;
        std::puts("native bottomless pages: full content height, stable page identity, atomic replacement/failed-update retention, stale layout rejection and exact cleanup PASS");
        columns=1;require(DestroyDocContext(context)==0&&DestroyInstalledObjectsInfo(installed)==0,"failed page creation leaves no retained resources");
        std::puts("native finite pages: actual callback geometry/ranges/break records, query/update state, foreign/stale handles, exact teardown, partial failure rollback and unsupported paths PASS");
        std::printf("native paragraph records: context=%zu, methods=%zu, callbacks=%zu; copied ownership, callbacks, rollback, invalid states and teardown PASS\n",sizeof(FSCONTEXTINFO),sizeof(FSIMETHODS),sizeof(FSCBK));return 0;
    }catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}
}
